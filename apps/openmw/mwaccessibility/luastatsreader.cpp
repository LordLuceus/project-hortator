#include "luastatsreader.hpp"

#include <iterator>

#include <sol/sol.hpp>

#include "../mwlua/localscripts.hpp"

#include "luastatsspeech.hpp"

namespace MWAccessibility
{
    namespace
    {
        // The mod's pane ids, read in this order. getPanes() returns a MAP, and
        // Lua map iteration order is arbitrary, so iterating it would let the
        // right pane's skills precede the left pane's vitals on some runs and
        // not others. Reading by explicit key keeps speech order stable and
        // matching the screen.
        constexpr std::string_view sPaneOrder[] = { "leftPane", "rightPane" };

        std::string readString(const sol::table& table, std::string_view key)
        {
            const sol::object value = table.get_or<sol::object>(key, sol::nil);
            return value.is<std::string>() ? value.as<std::string>() : std::string();
        }

        // Call a Lua predicate such as visibleFn, treating anything that is not
        // an explicit false as visible. A mod's predicate that errors or
        // returns nothing should not silently delete content from the window.
        bool callVisibleFn(const sol::table& table)
        {
            const sol::object fn = table.get_or<sol::object>("visibleFn", sol::nil);
            if (!fn.is<sol::function>())
                return true;

            const sol::protected_function_result result = sol::protected_function(fn.as<sol::function>())();
            if (!result.valid() || result.get_type() != sol::type::boolean)
                return true;

            return result.get<bool>();
        }

        // Resolve a line's value(), which returns { string = "..." }.
        std::string readValue(const sol::table& line)
        {
            const sol::object fn = line.get_or<sol::object>("value", sol::nil);
            if (!fn.is<sol::function>())
                return {};

            const sol::protected_function_result result = sol::protected_function(fn.as<sol::function>())();
            if (!result.valid())
                return {};

            const sol::object value = result;
            if (value.is<std::string>())
                return value.as<std::string>();
            if (!value.is<sol::table>())
                return {};

            const sol::table asTable = value.as<sol::table>();
            const std::string text = readString(asTable, "string");
            if (!text.empty())
                return text;

            // A progress-bar line reports current and maximum separately, and
            // the mod renders them as "current/maximum".
            const sol::object current = asTable.get_or<sol::object>("value", sol::nil);
            const sol::object maximum = asTable.get_or<sol::object>("maxValue", sol::nil);
            if (current.is<double>() && maximum.is<double>())
            {
                const auto format = [](double number) {
                    const long long rounded = static_cast<long long>(number);
                    return static_cast<double>(rounded) == number ? std::to_string(rounded) : std::to_string(number);
                };
                return format(current.as<double>()) + "/" + format(maximum.as<double>());
            }

            return {};
        }

        void readLayout(const sol::object& node, LuaLayoutNode& out, int depth)
        {
            // Layouts are shallow in practice; the bound stops a malformed or
            // self-referential table from recursing without end.
            if (depth > 16 || !node.is<sol::table>())
                return;

            const sol::table table = node.as<sol::table>();

            // The mod marks the datum-bearing part of a row with name="value",
            // which is the only reliable signal when a value is stacked below
            // its caption rather than beside it.
            out.mIsValue = (readString(table, "name") == "value");

            const sol::object props = table.get_or<sol::object>("props", sol::nil);
            if (props.is<sol::table>())
            {
                const sol::table propsTable = props.as<sol::table>();
                out.mText = readString(propsTable, "text");

                // A Flex laid out horizontally is one visual line, so its
                // descendants must be spoken as a single joined paragraph.
                const sol::object horizontal = propsTable.get_or<sol::object>("horizontal", sol::nil);
                out.mHorizontal = horizontal.is<bool>() && horizontal.as<bool>();
            }

            const sol::object content = table.get_or<sol::object>("content", sol::nil);
            if (!content.is<sol::table>())
                return;

            for (const auto& [key, child] : content.as<sol::table>())
            {
                if (!child.is<sol::table>())
                    continue;
                LuaLayoutNode childNode;
                readLayout(child, childNode, depth + 1);
                out.mChildren.push_back(std::move(childNode));
            }
        }

        // Resolve a line's tooltip() into spoken paragraphs.
        //
        // This is the only route to these at all: the mod attaches tooltips to
        // mouseMove and focusLoss but never to focusGain, so a keyboard user
        // never triggers one and there is no rendered widget to read. Calling
        // the builder directly produces the same text the mouse would show.
        std::vector<std::string> readTooltip(const sol::table& line)
        {
            const sol::object fn = line.get_or<sol::object>("tooltip", sol::nil);
            if (!fn.is<sol::function>())
                return {};

            const sol::protected_function_result result = sol::protected_function(fn.as<sol::function>())();
            if (!result.valid())
                return {};

            const sol::object layout = result;
            if (!layout.is<sol::table>())
                return {};

            LuaLayoutNode root;
            readLayout(layout, root, 0);
            return flattenTooltip(root);
        }

        LuaPlacement readPlacement(const sol::table& table)
        {
            LuaPlacement placement;

            const sol::object value = table.get_or<sol::object>("placement", sol::nil);
            if (!value.is<sol::table>())
                return placement;

            const sol::table source = value.as<sol::table>();

            const sol::object priority = source.get_or<sol::object>("priority", sol::nil);
            if (priority.is<double>())
                placement.mPriority = static_cast<int>(priority.as<double>());

            placement.mTarget = readString(source, "target");

            // constants.Placement: AFTER = 1, BEFORE = 2, TOP = 3, BOTTOM = 4.
            const sol::object type = source.get_or<sol::object>("type", sol::nil);
            if (type.is<double>())
            {
                switch (static_cast<int>(type.as<double>()))
                {
                    case 1:
                        placement.mType = LuaPlacementType::After;
                        break;
                    case 2:
                        placement.mType = LuaPlacementType::Before;
                        break;
                    case 3:
                        placement.mType = LuaPlacementType::Top;
                        break;
                    default:
                        placement.mType = LuaPlacementType::Bottom;
                        break;
                }
            }

            return placement;
        }

        LuaStatsLine readLine(const sol::table& source)
        {
            LuaStatsLine line;
            line.mId = readString(source, "id");
            line.mLabel = readString(source, "label");
            line.mVisible = callVisibleFn(source);
            line.mPlacement = readPlacement(source);

            // The value is resolved here so the option list can be built and
            // compared, but it is read again at speech time (readLineValue) so
            // a number can never be announced stale.
            //
            // Tooltips are NOT resolved here: they are only wanted when the
            // user presses T, and building every one up front would run
            // arbitrary mod code for every row in the window on every rebuild.
            // Skipped entirely for a hidden row, which often hides precisely
            // because its data is unavailable.
            if (line.mVisible)
                line.mValue = readValue(source);

            return line;
        }

        LuaStatsSection readSection(const sol::table& source, int depth)
        {
            LuaStatsSection section;
            section.mId = readString(source, "id");
            section.mHeader = readString(source, "header");
            section.mVisible = callVisibleFn(source);
            section.mPlacement = readPlacement(source);

            // constants.Sort: ADDED_ORDER = 1, LABEL_ASC = 2, LABEL_DESC = 3.
            const sol::object sort = source.get_or<sol::object>("sort", sol::nil);
            if (sort.is<double>())
            {
                const int value = static_cast<int>(sort.as<double>());
                if (value == 2)
                    section.mSort = LuaLineSort::LabelAscending;
                else if (value == 3)
                    section.mSort = LuaLineSort::LabelDescending;
            }

            if (!section.mVisible)
                return section;

            const sol::object lines = source.get_or<sol::object>("lines", sol::nil);
            if (lines.is<sol::table>())
            {
                for (const auto& [key, value] : lines.as<sol::table>())
                {
                    if (value.is<sol::table>())
                        section.mLines.push_back(readLine(value.as<sol::table>()));
                }
            }

            if (depth >= 8)
                return section;

            const sol::object sections = source.get_or<sol::object>("sections", sol::nil);
            if (sections.is<sol::table>())
            {
                for (const auto& [key, value] : sections.as<sol::table>())
                {
                    if (value.is<sol::table>())
                        section.mSections.push_back(readSection(value.as<sol::table>(), depth + 1));
                }
            }

            return section;
        }

        LuaStatsBox readBox(const sol::table& source, std::size_t paneIndex)
        {
            LuaStatsBox box;
            box.mPaneIndex = paneIndex;
            box.mId = readString(source, "id");
            box.mPlacement = readPlacement(source);

            const sol::object sections = source.get_or<sol::object>("sections", sol::nil);
            if (sections.is<sol::table>())
            {
                for (const auto& [key, value] : sections.as<sol::table>())
                {
                    if (value.is<sol::table>())
                        box.mSections.push_back(readSection(value.as<sol::table>(), 0));
                }
            }

            return box;
        }

        LuaStatsTree readTree(const sol::object& panes)
        {
            LuaStatsTree tree;
            if (!panes.is<sol::table>())
                return tree;

            const sol::table paneMap = panes.as<sol::table>();
            for (std::size_t paneIndex = 0; paneIndex < std::size(sPaneOrder); ++paneIndex)
            {
                const sol::object pane = paneMap.get_or<sol::object>(sPaneOrder[paneIndex], sol::nil);
                if (!pane.is<sol::table>())
                    continue;

                // A pane is an array of boxes.
                for (const auto& [key, value] : pane.as<sol::table>())
                {
                    if (value.is<sol::table>())
                        tree.mBoxes.push_back(readBox(value.as<sol::table>(), paneIndex));
                }
            }

            return tree;
        }
    }

    namespace LuaStatsReader
    {
        bool available()
        {
            return MWLua::LocalScripts::playerHasInterface(sInterfaceName);
        }

        std::optional<LuaStatsTree> read()
        {
            // The visitor runs while the Lua state is still safely borrowed, so
            // everything is copied out as plain C++ here; no sol reference
            // outlives this call.
            return MWLua::LocalScripts::visitPlayerInterface<LuaStatsTree>(
                sInterfaceName, "getPanes", [](const sol::object& result) { return readTree(result); });
        }

        std::optional<std::string> readLineValue(const std::string& lineId)
        {
            if (lineId.empty())
                return std::nullopt;

            // getLine returns the line plus its parents; only the first is
            // wanted here.
            return MWLua::LocalScripts::visitPlayerInterface<std::string>(
                sInterfaceName, "getLine",
                [](const sol::object& result) {
                    return result.is<sol::table>() ? readValue(result.as<sol::table>()) : std::string();
                },
                lineId);
        }

        std::vector<std::string> readLineTooltip(const std::string& lineId)
        {
            if (lineId.empty())
                return {};

            const std::optional<std::vector<std::string>> tooltip
                = MWLua::LocalScripts::visitPlayerInterface<std::vector<std::string>>(
                    sInterfaceName, "getLine",
                    [](const sol::object& result) {
                        return result.is<sol::table>() ? readTooltip(result.as<sol::table>())
                                                       : std::vector<std::string>();
                    },
                    lineId);

            return tooltip.value_or(std::vector<std::string>());
        }
    }
}

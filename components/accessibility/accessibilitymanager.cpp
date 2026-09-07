#include "accessibilitymanager.hpp"

#include <prism.h>

#include <components/debug/debuglog.hpp>

#include <string>

namespace Accessibility
{
    AccessibilityManager& AccessibilityManager::instance()
    {
        static AccessibilityManager sInstance;
        return sInstance;
    }

    AccessibilityManager::~AccessibilityManager()
    {
        shutdown();
    }

    bool AccessibilityManager::init()
    {
        if (mBackend)
            return true;

        PrismConfig cfg = prism_config_init();
        mContext = prism_init(&cfg);
        if (!mContext)
        {
            Log(Debug::Warning) << "Accessibility: prism_init() returned null; "
                                   "screen-reader output disabled.";
            return false;
        }

        mBackend = prism_registry_acquire_best(mContext);
        if (!mBackend)
        {
            Log(Debug::Warning) << "Accessibility: no Prism backend available; "
                                   "screen-reader output disabled.";
            prism_shutdown(mContext);
            mContext = nullptr;
            return false;
        }

        const PrismError err = prism_backend_initialize(mBackend);
        // ALREADY_INITIALIZED is benign for backends that proxy an external
        // process (NVDA, JAWS, etc.) -- the screen reader is already running.
        if (err != PRISM_OK && err != PRISM_ERROR_ALREADY_INITIALIZED)
        {
            Log(Debug::Warning) << "Accessibility: failed to initialise backend '"
                                << (prism_backend_name(mBackend) ? prism_backend_name(mBackend) : "<unknown>")
                                << "': " << prism_error_string(err);
            prism_backend_free(mBackend);
            mBackend = nullptr;
            prism_shutdown(mContext);
            mContext = nullptr;
            return false;
        }

        Log(Debug::Info) << "Accessibility: using TTS backend '" << backendName() << "'.";
        return true;
    }

    void AccessibilityManager::shutdown()
    {
        if (mBackend)
        {
            // Best-effort stop; ignore errors.
            (void)prism_backend_stop(mBackend);
            prism_backend_free(mBackend);
            mBackend = nullptr;
        }
        if (mContext)
        {
            prism_shutdown(mContext);
            mContext = nullptr;
        }
    }

    bool AccessibilityManager::speak(std::string_view text, bool interrupt)
    {
        if (!mBackend || text.empty())
            return false;

        // Prism takes a null-terminated C string; std::string_view is not
        // guaranteed to be null-terminated, so copy into a temp std::string.
        const std::string buf(text);
        // output() rather than speak(): on the screen readers that drive a braille
        // display (NVDA, JAWS) it both speaks the text and sends it to the
        // display, so a braille user gets the same output a speech user does. On
        // backends with no braille channel (SAPI, UIA) it is exactly speak(), so
        // this costs nothing where braille is unavailable.
        const PrismError err = prism_backend_output(mBackend, buf.c_str(), interrupt);
        if (err != PRISM_OK)
        {
            // NVDA and JAWS report the braille leg's failure even when the speech
            // leg succeeded, so this can fire on a machine with no display
            // connected. Log at Debug, not Warning: the audible output is fine and
            // the player has nothing to act on. No caller branches on the return
            // value, so reporting false here changes nothing but the log.
            Log(Debug::Debug) << "Accessibility: output failed: " << prism_error_string(err);
            return false;
        }
        return true;
    }

    void AccessibilityManager::stop()
    {
        if (mBackend)
            (void)prism_backend_stop(mBackend);
    }

    const char* AccessibilityManager::backendName() const
    {
        if (!mBackend)
            return "<none>";
        const char* name = prism_backend_name(mBackend);
        return name ? name : "<unnamed>";
    }
}

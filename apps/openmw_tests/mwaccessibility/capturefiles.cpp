#include "apps/openmw/mwaccessibility/capturefiles.hpp"

#include <components/sceneutil/screencapture.hpp>
#include <components/testing/util.hpp>

#include <osg/Image>
#include <osgDB/ReadFile>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace MWAccessibility
{
    TEST(MWAccessibilityCaptureFiles, EncodesWindowsPathWithoutShellInterpolation)
    {
        EXPECT_EQ(captureFolderUri("C:/Pictures/a #b%&.png"), "file:///C:/Pictures/a%20%23b%25%26.png");
    }

    TEST(MWAccessibilityCaptureFiles, HandlesPosixAndUncPaths)
    {
        EXPECT_EQ(captureFolderUri("/home/player/pictures"), "file:///home/player/pictures");
        EXPECT_EQ(captureFolderUri("//server/share/a b"), "file://server/share/a%20b");
    }

    TEST(MWAccessibilityCaptureFiles, EncodesUtf8Bytes)
    {
        EXPECT_EQ(captureFolderUri("C:/caf\xc3\xa9"), "file:///C:/caf%C3%A9");
    }

    TEST(MWAccessibilityCaptureFiles, PngWriterReturnsRelativeFilenameAndDoesNotOverwrite)
    {
        const auto folder = TestingOpenMW::outputDir() / "scene-capture";
        std::filesystem::create_directories(folder);
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(2, 2, 1, GL_RGB, GL_UNSIGNED_BYTE);
        for (unsigned i = 0; i < image->getTotalSizeInBytes(); ++i)
            image->data()[i] = static_cast<unsigned char>(i * 20);
        const auto first = SceneUtil::writeScreenshotToFile(folder, "png", *image);
        const auto second = SceneUtil::writeScreenshotToFile(folder, "png", *image);
        ASSERT_FALSE(first.empty());
        ASSERT_FALSE(second.empty());
        EXPECT_TRUE(first.is_relative());
        EXPECT_NE(first, second);
        const auto decoded = osgDB::readRefImageFile((folder / first).string());
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->s(), 2);
        EXPECT_EQ(decoded->t(), 2);
    }

    TEST(MWAccessibilityCaptureFiles, UnwritableDestinationIsNotReportedAsSuccess)
    {
        const auto folder = TestingOpenMW::outputDir() / "scene-capture-file-not-directory";
        {
            std::ofstream file(folder);
        }
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE);
        EXPECT_TRUE(SceneUtil::writeScreenshotToFile(folder, "png", *image).empty());
    }
}

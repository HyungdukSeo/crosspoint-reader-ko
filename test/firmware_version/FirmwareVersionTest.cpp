#include <gtest/gtest.h>

#include "src/network/FirmwareVersion.h"

using firmware_version::isNewer;

TEST(FirmwareVersion, TwoPartReleaseUpdates) {
  EXPECT_TRUE(isNewer("15.05", "15.04"));
  EXPECT_TRUE(isNewer("15.10", "15.09"));
  EXPECT_FALSE(isNewer("15.04", "15.04"));
  EXPECT_FALSE(isNewer("15.04", "15.05"));
  EXPECT_FALSE(isNewer("15.4.0", "15.04"));
}

TEST(FirmwareVersion, HistoricalVersions) {
  EXPECT_TRUE(isNewer("15.04", "1.5.0-ko.3"));
  EXPECT_TRUE(isNewer("1.5.0-ko.4", "1.5.0-ko.3"));
  EXPECT_FALSE(isNewer("1.5.0-ko.3", "1.5.0-ko.4"));
  EXPECT_TRUE(isNewer("15.04", "15.04-rc+abcd"));
  EXPECT_FALSE(isNewer("15.04-rc", "15.04"));
  EXPECT_FALSE(isNewer("15.04+abcd", "15.04"));
  EXPECT_TRUE(isNewer("v15.05", "15.04"));
}

TEST(FirmwareVersion, InvalidTagsDoNotOfferUpdates) {
  for (const auto* bad : {"", "latest", "15", "15.", "15.05junk", "15.05-ko.", "99999999999999.1", "15.05+"}) {
    EXPECT_FALSE(isNewer(bad, "15.04")) << bad;
    EXPECT_FALSE(isNewer("15.05", bad)) << bad;
  }
}

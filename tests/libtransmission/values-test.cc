// This file Copyright © ReTransmission contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// License text can be found in the licenses/ folder.

#include <libtransmission/values.h>

#include "gtest/gtest.h"

using namespace libtransmission::Values;

using ValuesTest = ::testing::Test;

TEST_F(ValuesTest, baseQuantity)
{
    auto const val = Speed{ 1, Speed::Units::MByps };
    EXPECT_EQ(1000000UL, val.base_quantity());
}

TEST_F(ValuesTest, count)
{
    auto const val = Speed{ 1, Speed::Units::MByps };
    EXPECT_NEAR(1000U, val.count(Speed::Units::KByps), 0.0001);
    EXPECT_NEAR(1U, val.count(Speed::Units::MByps), 0.0001);
    EXPECT_NEAR(0.001, val.count(Speed::Units::GByps), 0.0001);
}

TEST_F(ValuesTest, toString)
{
    auto val = Speed{ 1, Speed::Units::MByps };
    EXPECT_EQ("1 MB/s", val.to_string());

    val = Speed{ 1, Speed::Units::Byps };
    EXPECT_EQ(1U, val.base_quantity());
    EXPECT_EQ("1 B/s", val.to_string());

    val = Speed{ 10, Speed::Units::KByps };
    EXPECT_EQ("10 kB/s", val.to_string());

    val = Speed{ 999, Speed::Units::KByps };
    EXPECT_EQ("999 kB/s", val.to_string());

    val = Speed{ 99.22222, Speed::Units::KByps };
    EXPECT_EQ("99.22 kB/s", val.to_string());

    val = Speed{ 999.22222, Speed::Units::KByps };
    EXPECT_EQ("999.2 kB/s", val.to_string());
}

TEST_F(ValuesTest, isZero)
{
    auto val = Speed{};
    EXPECT_TRUE(val.is_zero());

    val = Speed{ 0, Speed::Units::Byps };
    EXPECT_TRUE(val.is_zero());

    val = Speed{ 1, Speed::Units::Byps };
    EXPECT_FALSE(val.is_zero());
}

#include <gtest/gtest.h>

#include <stdexcept>

#include "payment_terminal.h"

class PaymentTerminalTest : public ::testing::Test {
protected:
    PaymentTerminal terminal{100.0};  // card with $100
};

TEST_F(PaymentTerminalTest, StartsWithNoHold) {
    EXPECT_FALSE(terminal.isAuthorized());
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), 100);
}

TEST_F(PaymentTerminalTest, RejectsNegativeCardBalance) {
    EXPECT_THROW(PaymentTerminal(-1), std::invalid_argument);
}

TEST_F(PaymentTerminalTest, AuthorizeWithinBalanceSucceeds) {
    EXPECT_TRUE(terminal.authorize(40));
    EXPECT_TRUE(terminal.isAuthorized());
    EXPECT_DOUBLE_EQ(terminal.authorizedAmount(), 40);
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), 100);  // a hold does not spend money
}

TEST_F(PaymentTerminalTest, AuthorizeAboveBalanceIsDeclined) {
    EXPECT_FALSE(terminal.authorize(150));
    EXPECT_FALSE(terminal.isAuthorized());
}

TEST_F(PaymentTerminalTest, AuthorizeRejectsNonPositiveAmount) {
    EXPECT_THROW(terminal.authorize(0), std::invalid_argument);
    EXPECT_THROW(terminal.authorize(-5), std::invalid_argument);
}

TEST_F(PaymentTerminalTest, CannotAuthorizeTwice) {
    terminal.authorize(40);
    EXPECT_FALSE(terminal.authorize(10));
    EXPECT_DOUBLE_EQ(terminal.authorizedAmount(), 40);
}

TEST_F(PaymentTerminalTest, CaptureDeductsFromBalanceAndClearsHold) {
    terminal.authorize(40);
    EXPECT_TRUE(terminal.capture(25));
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), 75);
    EXPECT_FALSE(terminal.isAuthorized());
}

TEST_F(PaymentTerminalTest, CaptureWithoutHoldFails) {
    EXPECT_FALSE(terminal.capture(10));
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), 100);
}

TEST_F(PaymentTerminalTest, CannotCaptureMoreThanHold) {
    terminal.authorize(40);
    EXPECT_FALSE(terminal.capture(41));
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), 100);
    EXPECT_TRUE(terminal.isAuthorized());
}

TEST_F(PaymentTerminalTest, CaptureRejectsNegativeAmount) {
    terminal.authorize(40);
    EXPECT_THROW(terminal.capture(-1), std::invalid_argument);
}

TEST_F(PaymentTerminalTest, ReleaseClearsHoldWithoutSpending) {
    terminal.authorize(40);
    terminal.release();
    EXPECT_FALSE(terminal.isAuthorized());
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), 100);
}

TEST_F(PaymentTerminalTest, OfflineTerminalDeclinesAuthorize) {
    terminal.setOffline(true);
    EXPECT_TRUE(terminal.isOffline());
    EXPECT_FALSE(terminal.authorize(10));
}

TEST_F(PaymentTerminalTest, OfflineTerminalFailsCapture) {
    terminal.authorize(40);
    terminal.setOffline(true);
    EXPECT_FALSE(terminal.capture(10));
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), 100);
}

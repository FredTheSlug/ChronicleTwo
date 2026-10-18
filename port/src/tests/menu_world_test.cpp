#include <gtest/gtest.h>

#include "menu_world.hpp"
#include "menumain.hpp"

// Menus other than the town and dungeon main menus open at once, and nothing of the Item page changes.
TEST(MenuWorld, OtherMenusOpenUnchanged) {
    MENU_INIT_ARG arg{};
    arg.open_type = MENU_OPEN_INVENT;
    EXPECT_FALSE(menuworld::HoldMenuInit(&arg));
    EXPECT_FALSE(menuworld::HoldMenuInit(nullptr));
    EXPECT_FALSE(menuworld::Framing());
    EXPECT_FALSE(menuworld::ItemBackdrop());

    const menuworld::ItemDrawScope scope;
    EXPECT_FALSE(menuworld::FillWithBackdrop());
    EXPECT_FALSE(menuworld::SelectionAnchor().has_value());
    EXPECT_FLOAT_EQ(menuworld::RetailPointerX(123.5f), 123.5f);
}

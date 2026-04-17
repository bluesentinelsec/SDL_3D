#include <gtest/gtest.h>

extern "C"
{
#include "sdl3d/sdl3d.h"
}

#include <string_view>

TEST(SDL3D, GreetReturnsExpectedMessage)
{
    EXPECT_EQ(std::string_view("Hello from SDL3D."), sdl3d_greet());
}

package com.sdl3d.tests;

import org.libsdl.app.SDLActivity;

/**
 * Hosts the SDL3D GoogleTest binary. Gives SDLActivity a real surface
 * so the native layer can initialize the video subsystem and run the
 * render-labeled tests.
 */
public class TestActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "main",
        };
    }
}

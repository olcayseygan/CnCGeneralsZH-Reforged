# Guided setup

On macOS or Linux, run `make install` from the repository root in a terminal. Python 3 and make are required. The prompts are in English or German, based on the locale; set `ZH_INSTALL_LANG=en` or `ZH_INSTALL_LANG=de` to choose one.

The setup can check build tools, invoke the existing build and tests, find or copy an owned game installation, and launch the result. Each action asks for consent. Building may download third-party dependencies. Game data is not included or downloaded, and any copy is kept outside the checkout.

On macOS the setup can copy a built, signed application to `/Applications` or the current user's `Applications` folder. It verifies the selected bundle and its signature, asks before replacement, and attempts to restore the old application if installation fails. On Linux it can run the built package's personal menu integration.

For automated builds use `build-macos.sh` or `build-linux.sh` directly. `ZH_BUILD_CONFIG` selects the build configuration and `BUILD` can select a different build directory.

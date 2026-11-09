# GameSpy-compatible service configuration

Legacy peer and statistics connections require a compatible service title and key. Supply `ZH_GAMESPY_TITLE` and `ZH_GAMESPY_SECRET_KEY` through the game process environment. The Zero Hour title remains the default when the title variable is absent; a key must be supplied. Fields accept 1–31 ASCII letters, digits, underscores or hyphens. Invalid or missing values cause the connection to fail.

The legacy matchbot reads its title and key from its `LOGIN` configuration. Keep keys private and out of source control.

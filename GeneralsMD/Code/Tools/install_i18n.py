#!/usr/bin/env python3
"""Message lookup shared by the guided installer and the build script's prompts.

English is the source language: every message is its own key. A translation is
a JSON file in installer_locales/<language>.json (see docs/installer.md).

    install_i18n.py confirm "Question %s?" arg...   exit 0 = yes, 1 = no, 2 = prompt error, 130 = input closed
"""
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
import json
import os
import re
import sys
from pathlib import Path

LOCALES = Path(__file__).resolve().parent / 'installer_locales'
ENGLISH_YES = ('y', 'yes')
PLACEHOLDERS = re.compile(r'%(?:%|[sd])')


def language(environ=None):
    """Two/three-letter language code from ZH_INSTALL_LANG, LC_ALL, LC_MESSAGES or LANG."""
    environ = os.environ if environ is None else environ
    for name in ('ZH_INSTALL_LANG', 'LC_ALL', 'LC_MESSAGES', 'LANG'):
        value = environ.get(name, '').strip()
        if value:
            # C and POSIX select the untranslated English messages.
            match = re.match(r'[A-Za-z]{2,3}(?![A-Za-z])', value)
            return match.group(0).lower() if match else 'en'
    return 'en'


def load(code):
    if not re.fullmatch(r'[a-z]{2,3}', code or '') or code == 'en':
        return {}
    try:
        with open(LOCALES / (code + '.json'), encoding='utf-8') as handle:
            catalog = json.load(handle)
    except (OSError, ValueError):
        return {}
    if not isinstance(catalog, dict):
        return {}
    messages, yes, hint = catalog.get('messages', {}), catalog.get('yes', []), catalog.get('hint', '[y/N]')
    if not isinstance(messages, dict) or not all(isinstance(text, str) for text in messages.values()):
        return {}
    if not isinstance(yes, list) or not all(isinstance(word, str) for word in yes):
        return {}
    if not isinstance(hint, str) or not hint.strip():
        return {}
    return catalog


class Translator:
    def __init__(self, code=None):
        self.code = code or language()
        catalog = load(self.code)
        self.messages = catalog.get('messages', {})
        self.yes = {word.strip().casefold() for word in catalog.get('yes', [])} | set(ENGLISH_YES)
        self.hint = catalog.get('hint', '[y/N]')

    def __call__(self, message, *args):
        text = self.messages.get(message, message)
        if PLACEHOLDERS.findall(text) != PLACEHOLDERS.findall(message):
            text = message
        if not args:
            return text
        try:
            return text % args
        except (TypeError, ValueError):
            return message % args

    def is_yes(self, answer):
        normalized = answer.strip().casefold()
        return bool(normalized) and normalized not in ('n', 'no') and normalized in self.yes


translator = Translator()


def confirm_main(argv):
    if not argv:
        return 2
    _ = translator
    try:
        answer = input('\n%s %s ' % (_(argv[0], *argv[1:]), _.hint))
    except EOFError:
        print()
        return 130
    except (OSError, TypeError, ValueError) as error:
        print('Setup confirmation failed: %s' % error, file=sys.stderr)
        return 2
    return 0 if _.is_yes(answer) else 1


if __name__ == '__main__':
    if sys.argv[1:2] == ['confirm']:
        sys.exit(confirm_main(sys.argv[2:]))
    print('usage: install_i18n.py confirm MESSAGE [ARG...]', file=sys.stderr)
    sys.exit(2)

#!/usr/bin/python3

import codecs
import functools
import os
import re
import sys

from pathlib import Path


SOURCE_DIR = 'src'
PREFIX = 'TWILIGHT_'
PATTERNS = [ '**/*.h' ]

REGEX_GUARD = re.compile(r'^#ifndef ([^ ]+)\r?\n#define \1$', re.M)
REGEX_ENDIF = re.compile(r'^#endif', re.M)
REGEX_NOT_IDENTIFIER = re.compile(r'[^_a-zA-Z0-9]')

EXPECTS_NONE = 0
EXPECTS_COMMENT_START = 1
EXPECTS_BLOCK_COMMENT_END = 2

RETURN_SUCCESS = 0
RETURN_WARNING = 1
RETURN_ERROR = 2


def is_empty(content):
    expects = EXPECTS_NONE
    now_block_comment = False
    now_inline_comment = False

    for ch in content:
        if ch == '\n':
            expects = EXPECTS_NONE
            now_inline_comment = False
        elif ch.isspace():
            expects = EXPECTS_NONE
        else:
            if expects == EXPECTS_COMMENT_START and ch == '*':
                now_block_comment = True
            elif expects == EXPECTS_COMMENT_START and ch == '/':
                now_inline_comment = True
            elif expects == EXPECTS_BLOCK_COMMENT_END and ch == '/':
                now_block_comment = False
            elif now_block_comment and ch == '*':
                expects = EXPECTS_BLOCK_COMMENT_END
            elif not now_block_comment and not now_inline_comment and ch == '/':
                expects = EXPECTS_COMMENT_START
            elif not now_block_comment and not now_inline_comment:
                return False

    return True


def process_file(path, fix=False):
    path = Path(os.path.abspath(path))
    name = path.name

    try:
        with codecs.open(path, 'r', encoding='utf-8') as f:
            content = f.read(134217728)  # 128 MiB
            if len(f.read(1)) != 0:
                print(f'File too large: {name}')
                return
    except UnicodeDecodeError:
        print(f'File not UTF-8: {name}')
        return RETURN_WARNING

    guards = list(REGEX_GUARD.finditer(content))
    endifs = list(REGEX_ENDIF.finditer(content))

    if len(guards) < 1 or len(endifs) < 1:
        print(f'Contains no header guard: {name}')
        return RETURN_WARNING

    idx, _ = guards[0].span(0)
    if not is_empty(content[:idx]):
        print(f'Contains something before #ifndef: {name}')
        return RETURN_WARNING

    _, idx = endifs[-1].span(0)
    if not is_empty(content[idx:]):
        print(f'Contains something after #endif: {name}')
        return RETURN_WARNING

    expected = functools.reduce(lambda x, y: '' if y == SOURCE_DIR else f'{x}_{y.upper()}', path.parts)
    if expected[0] == '_':
        expected = expected[1:]
    expected = expected.replace('.', '_')
    expected = REGEX_NOT_IDENTIFIER.sub('', expected)
    expected = PREFIX + expected

    guard = guards[0].group(1).strip()

    if guard != expected:
        print(f'Wrong header guard: {name}')
        print(f'  Got {guard}')
        print(f'  Expected {expected}')
        if fix:
            if content.count(guard) != 2:
                print(f'  Multiple occurances of {guard}. Refusing to fix the file.')
            else:
                print('  Fixed.')
                content = content.replace(guard, expected)
                with codecs.open(path, 'w', encoding='utf-8') as f:
                    f.write(content)
        return RETURN_ERROR

    return RETURN_SUCCESS


def process_dir(path, fix=False):
    path = Path(path)
    success = 0
    warning = 0
    error = 0

    for pattern in PATTERNS:
        for file in path.glob(pattern):
            ret = process_file(file, fix=fix)
            if ret == RETURN_SUCCESS:
                success += 1
            elif ret == RETURN_WARNING:
                warning += 1
            elif ret == RETURN_ERROR:
                error += 1

    if warning > 0 or error > 0:
        print(f'{success} Success  {warning} Warning  {error} Error')
        return 1
    return 0


def print_usage():
    print('Usage: fix-header-guard.py [--fix] <path>')
    print('  --fix  : Try to fix the file.')
    print('  <path> : The top level path to operate on.')
    print('        Usually the project dir or src dir.')


if __name__ == '__main__':
    if len(sys.argv) < 2 or 3 < len(sys.argv):
        print_usage()
        sys.exit(1)
    if len(sys.argv) == 3 and sys.argv[1] != '--fix':
        print_usage()
        sys.exit(1)
    
    should_fix = sys.argv[1] == '--fix'

    sys.exit(process_dir(sys.argv[-1], fix=should_fix))

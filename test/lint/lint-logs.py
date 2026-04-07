#!/usr/bin/env python3
#
# Check that log messages do NOT contain '\n' in the format string.
#
# The applog() and gpulog() functions automatically add a newline
# at the end of each message. Adding '\n' in the format string
# will result in double newlines in the output.

import re
import sys

from subprocess import check_output, CalledProcessError


def main():
    # Search for applog() and gpulog() calls in C/C++ source files
    try:
        logs_raw = check_output(
            ["git", "grep", "--extended-regexp", r"\b(applog|gpulog)\s*\(", "--", "*.cpp", "*.c", "*.h"],
            text=True, encoding="utf8"
        )
        logs_list = logs_raw.splitlines()
    except CalledProcessError:
        # No matches found - this is fine, nothing to lint
        logs_list = []

    # Check for \n in the format string (between the quotes after the priority argument)
    # This pattern looks for \\n inside quotes in the log call
    logs_with_newline = [line for line in logs_list if re.search(r'(applog|gpulog)\s*\([^,]+,\s*"[^"]*\\n', line)]

    if logs_with_newline:
        print("Log messages should NOT contain '\\n' in the format string.")
        print("The applog() and gpulog() functions automatically add a newline.")
        print("")

        for line in logs_with_newline:
            print(line)

        sys.exit(1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""zipcheck.py: the entries of a release zip, read from its central directory as they are written there (PL-D82),
for windows.ps1.

    zipcheck.py <zip> --folder <name> --files <file>...

The names are the raw ones of the central directory (zipfile's orig_filename: zipfile's filename turns a "\\" into
"/" on Windows, and so does tar.exe -t, Windows' bsdtar; a listing through either cannot see one). It holds when:
  - no entry name has a "\\" (Compress-Archive of Windows PowerShell 5.1 writes "<name>\\LICENSE");
  - the folder's own entry "<name>/" is there (tar.exe -a -c writes it; Compress-Archive does not);
  - every other entry is "<name>/<file>", and the files are exactly --files (no other entry, none below a folder).
Every entry is printed, then one line PASS or FAIL. Exit status 0 when it holds, 1 when not, 2 when the zip cannot be
read. Python 3.9, the standard library only.
"""
import argparse
import sys
import zipfile


def problems_of(names, folder, files):
    """What is wrong with the entry names of a zip: a list of texts, empty when nothing is."""
    problems = []
    backslash = [name for name in names if '\\' in name]
    if backslash:
        problems.append('entries with a backslash: %s' % ', '.join(backslash))
    top = folder + '/'
    if top not in names:
        problems.append('no entry %s of the folder itself' % top)
    outside = [name for name in names if not name.startswith(top) and '\\' not in name]
    if outside:
        problems.append('entries outside %s: %s' % (top, ', '.join(outside)))
    below = sorted(name[len(top):] for name in names if name.startswith(top) and name != top)
    if below != sorted(files):
        problems.append('the files below %s are [%s], expected [%s]' % (top, ', '.join(below), ', '.join(sorted(files))))
    if len(set(names)) != len(names):
        problems.append('an entry twice')
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('zip')
    parser.add_argument('--folder', required=True)
    parser.add_argument('--files', nargs='+', required=True)
    options = parser.parse_args()
    try:
        with zipfile.ZipFile(options.zip) as archive:
            names = [info.orig_filename for info in archive.infolist()]
    except (OSError, zipfile.BadZipFile) as error:
        print('FAIL the zip %s cannot be read: %s' % (options.zip, error))
        return 2
    print("the zip's entries (its central directory):")
    for name in names:
        print('  %s' % name)
    problems = problems_of(names, options.folder, options.files)
    if problems:
        print('FAIL the zip: %s' % '; '.join(problems))
        return 1
    print('PASS the zip: the folder %s/ and below it %s, every name with / alone' % (options.folder, ', '.join(sorted(options.files))))
    return 0


if __name__ == '__main__':
    sys.exit(main())

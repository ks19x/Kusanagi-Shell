#!/usr/bin/env python3
"""labwc-keys.py — put Kusanagi's keybinds into labwc's rc.xml (labwc has no include for it).

  python3 lib/labwc-keys.py merge <rc.xml> <block.xml>   replace / add the block in rc.xml
  python3 lib/labwc-keys.py remove <rc.xml>              take it out again

The block is the text between "<!-- kusanagi:begin" and "kusanagi:end -->" (install.sh writes it). It goes
at the end of <keyboard>, so its keys come after yours: labwc keeps the last keybind for a key. When your
<keyboard> has no keybinds of its own, labwc was using its default ones; a <default /> keeps them.
Only the text is touched (your formatting and comments stay), and the result must parse as XML or
rc.xml isn't written at all (exit 1, the reason on stderr).
"""
import os
import re
import sys
import tempfile
import xml.dom.minidom

BLOCK = re.compile(r"[ \t]*<!-- kusanagi:begin.*?kusanagi:end -->[ \t]*\n?", re.S)
COMMENT = re.compile(r"<!--.*?-->", re.S)
EMPTY = '<?xml version="1.0"?>\n<labwc_config>\n</labwc_config>\n'


def fail(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def parses(text):
    try:
        xml.dom.minidom.parseString(text.encode())
        return True
    except Exception:
        return False


def merge(text, block):
    text = BLOCK.sub("", text)
    if not text.strip():
        text = EMPTY
    if not parses(text):
        fail("rc.xml isn't valid XML")
    text = re.sub(r"<keyboard(\s[^>]*)?/>", lambda m: "<keyboard%s>\n  </keyboard>" % (m.group(1) or ""), text, count=1)
    end = text.rfind("</keyboard>")
    if end < 0:
        root = text.rfind("</labwc_config>")
        if root < 0:
            root = text.rfind("</openbox_config>")  # very old configs
        if root < 0:
            fail("no <labwc_config> in rc.xml")
        text = text[:root] + "  <keyboard>\n  </keyboard>\n" + text[root:]
        end = text.rfind("</keyboard>")
    start = text.rfind("<keyboard", 0, end)
    own = COMMENT.sub("", text[start:end])
    if "<keybind" not in own and "<default" not in own:
        # labwc only loads its default keys when there are no keybinds at all (or a <default />)
        block = block.replace("-->\n", "-->\n    <default />\n", 1)
    # start the block on a line of its own
    line_start = text.rfind("\n", 0, end) + 1
    if text[line_start:end].strip():
        text = text[:end] + "\n" + text[end:]
        end += 1
        line_start = end
    if not block.endswith("\n"):
        block += "\n"
    out = text[:line_start] + block + text[line_start:]
    if not parses(out):
        fail("the merged rc.xml wouldn't be valid XML")
    return out


def write(path, text):
    d = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".rc.xml.")
    with os.fdopen(fd, "w") as f:
        f.write(text)
    if os.path.exists(path):
        os.chmod(tmp, os.stat(path).st_mode & 0o777)
    os.replace(tmp, path)


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("merge", "remove") or (sys.argv[1] == "merge" and len(sys.argv) < 4):
        fail(__doc__.strip().splitlines()[2])
    path = sys.argv[2]
    text = open(path).read() if os.path.exists(path) else ""
    if sys.argv[1] == "remove":
        if BLOCK.search(text):
            write(path, BLOCK.sub("", text))
        return
    block = open(sys.argv[3]).read()
    if "kusanagi:begin" not in block:
        fail("the block has no kusanagi:begin marker")
    write(path, merge(text, block))


if __name__ == "__main__":
    main()

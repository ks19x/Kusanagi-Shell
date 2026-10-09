#!/usr/bin/env python3
"""bt-agent.py — a BlueZ pairing agent for Kusanagi (Bt.qml runs it only while Bluetooth is open or pairing).

Devices that want a code confirmed or typed (phones, keyboards) ask the agent; this one hands each
question to the shell as a JSON line on stdout and waits for the answer on stdin:
  out: {"ev": "ready"} · {"ev": "confirm", "device": path, "passkey": "123456"} · {"ev": "authorize", "device": path}
       {"ev": "pin", "device": path} · {"ev": "passkey", "device": path} · {"ev": "display", "device": path, "code": "…"}
       {"ev": "cancel"} · {"ev": "error", "message": "…"}
  in:  yes · no · pin <text> · passkey <number>
Needs PyGObject (python3-gi / python-gobject). Exits when stdin closes (the shell stopped it).
"""
import json
import sys

try:
    from gi.repository import Gio, GLib
except ImportError:
    print(json.dumps({"ev": "error", "message": "PyGObject isn't installed"}), flush=True)
    sys.exit(1)

PATH = "/org/kusanagi/agent"
XML = """<node><interface name="org.bluez.Agent1">
  <method name="Release"/>
  <method name="RequestPinCode"><arg type="o" direction="in"/><arg type="s" direction="out"/></method>
  <method name="DisplayPinCode"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
  <method name="RequestPasskey"><arg type="o" direction="in"/><arg type="u" direction="out"/></method>
  <method name="DisplayPasskey"><arg type="o" direction="in"/><arg type="u" direction="in"/><arg type="q" direction="in"/></method>
  <method name="RequestConfirmation"><arg type="o" direction="in"/><arg type="u" direction="in"/></method>
  <method name="RequestAuthorization"><arg type="o" direction="in"/></method>
  <method name="AuthorizeService"><arg type="o" direction="in"/><arg type="s" direction="in"/></method>
  <method name="Cancel"/>
</interface></node>"""

loop = GLib.MainLoop()
waiting = None          # (invocation, method) answered by the next stdin line


def say(**ev):
    print(json.dumps(ev), flush=True)


def reject(inv, why="Rejected"):
    inv.return_dbus_error("org.bluez.Error." + why, why)


def paired(bus, path):
    try:
        v = bus.call_sync("org.bluez", path, "org.freedesktop.DBus.Properties", "Get",
                          GLib.Variant("(ss)", ("org.bluez.Device1", "Paired")), GLib.VariantType("(v)"),
                          Gio.DBusCallFlags.NONE, 2000, None)
        return bool(v.unpack()[0])
    except GLib.Error:
        return False


def on_call(bus, sender, path, iface, method, args, inv):
    global waiting
    a = args.unpack()
    if method == "Release":
        inv.return_value(None)
        loop.quit()
        return
    if method == "Cancel":
        if waiting:
            reject(waiting[0], "Canceled")
            waiting = None
        say(ev="cancel")
        inv.return_value(None)
        return
    if method in ("DisplayPinCode", "DisplayPasskey"):
        code = a[1] if method == "DisplayPinCode" else "%06d" % a[1]
        say(ev="display", device=a[0], code=code)
        inv.return_value(None)
        return
    if method == "AuthorizeService" and paired(bus, a[0]):
        inv.return_value(None)          # a device you paired, reconnecting a profile: fine
        return
    if waiting:                         # one question at a time
        reject(waiting[0], "Canceled")
    waiting = (inv, method)
    if method == "RequestConfirmation":
        say(ev="confirm", device=a[0], passkey="%06d" % a[1])
    elif method == "RequestPinCode":
        say(ev="pin", device=a[0])
    elif method == "RequestPasskey":
        say(ev="passkey", device=a[0])
    else:                               # RequestAuthorization, AuthorizeService
        say(ev="authorize", device=a[0])


def on_stdin(src, cond):
    global waiting
    line = sys.stdin.readline()
    if not line:
        loop.quit()
        return False
    word, _, rest = line.strip().partition(" ")
    if waiting:
        inv, method = waiting
        waiting = None
        if word == "no":
            reject(inv)
        elif method == "RequestPinCode":
            inv.return_value(GLib.Variant("(s)", (rest or "0000",)))
        elif method == "RequestPasskey":
            try:
                inv.return_value(GLib.Variant("(u)", (int(rest or "0"),)))
            except ValueError:
                reject(inv)
        else:
            inv.return_value(None)
    return True


def main():
    bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
    node = Gio.DBusNodeInfo.new_for_xml(XML)
    bus.register_object(PATH, node.interfaces[0], on_call, None, None)
    mgr = lambda m, sig, val: bus.call_sync("org.bluez", "/org/bluez", "org.bluez.AgentManager1", m,
                                            GLib.Variant(sig, val), None, Gio.DBusCallFlags.NONE, 5000, None)
    try:
        mgr("RegisterAgent", "(os)", (PATH, "KeyboardDisplay"))
        mgr("RequestDefaultAgent", "(o)", (PATH,))
    except GLib.Error as e:
        say(ev="error", message=e.message)
        sys.exit(1)
    GLib.io_add_watch(sys.stdin.fileno(), GLib.IO_IN | GLib.IO_HUP, on_stdin)
    say(ev="ready")
    try:
        loop.run()
    finally:
        try:
            mgr("UnregisterAgent", "(o)", (PATH,))
        except GLib.Error:
            pass


if __name__ == "__main__":
    main()

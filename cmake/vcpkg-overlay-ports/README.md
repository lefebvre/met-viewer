# vcpkg overlay ports

Overlay ports (`VCPKG_OVERLAY_PORTS`) used by the `dist-linux`/`dist-windows`
presets to patch transitive dependencies for CI.

## dbus

A copy of the upstream `dbus` port (pinned to the manifest baseline) with the
`systemd` **default feature removed** (`"default-features": []`).

Upstream enables `dbus[systemd]` by default on Linux, which pulls in
`libsystemd`. Recent `libsystemd` (systemd 260) requires `sys/pidfd.h`
(glibc ≥ 2.36) and cannot build on the AlmaLinux 9 CI container (glibc 2.34).

met-viewer uses no D-Bus at runtime — the target Qt build already excludes it —
but the **host** Qt build (moc/rcc/uic tools) is built with Qt's default
features, which include `dbus`, dragging `libsystemd` into the graph. Dropping
the `systemd` feature (it only adds "at_console support") removes `libsystemd`
entirely; dbus and the Qt host tools build fine without it.

To refresh against a newer vcpkg baseline, re-copy `vcpkg/ports/dbus` here and
re-apply the one-line `default-features` change.

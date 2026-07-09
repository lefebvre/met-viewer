MET VIEWER — ICON SET (PNG)
===========================

Transparent PNGs for the Qt6 met-viewer toolbar, mode switcher, layer panel
and playback controls. 39 UI glyphs + the application icon.

Layout
------
  dark/   glyphs in near-black (#1F2933) — for LIGHT toolbars
  light/  glyphs in off-white (#EEF2F7) — for DARK toolbars
  app/    application icon (full color)

Each glyph is provided at 16, 24, 32, 48 px  (1x + 2x for 16 & 24 px toolbars).
The app icon is provided at 16 / 24 / 32 / 48 / 64 / 128 / 256 / 512 px.

File naming
-----------
  <token>_<size>.png      e.g.  wind-barb_24.png,  base-osm_48.png

Tokens map to the app features described in Design.md:

  Modes        view-pan  mode-probe  mode-section  mode-skewt  mode-tseries
  Data/files   file-open  data-grid  view-layers  axis-level  axis-time
  Visualize    render-cmap  view-cbar  render-contours  view-plot2d  view-map  layer-opacity
  Wind         wind-barb  wind-arrow  wind-streamlines
  Basemap      base-osm  base-imagery  base-terrain  base-light  base-dark  base-custom
  Overlays     overlay-coast  overlay-borders  overlay-graticule
  Animation    anim-play  anim-pause  anim-prev  anim-next  anim-loop  anim-fps
  General      view-zoomin  view-zoomout  view-fit  app-settings  app-grab

Using in Qt6 (high-DPI)
-----------------------
Qt auto-picks the @2x variant when you add both sizes to one QIcon:

  QIcon icon;
  icon.addFile(":/icons/dark/wind-barb_24.png");   // 1x
  icon.addFile(":/icons/dark/wind-barb_48.png");   // 2x (QIcon treats 48 as the 24@2x)

Or just load the size you need per device-pixel-ratio. For a fully scalable
alternative, the source SVG geometry lives in "Met Viewer Icon System.dc.html".
Drawn on a 24x24 grid, 1.8 px stroke, round caps/joins.

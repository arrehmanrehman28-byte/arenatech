# Arena Edit — assets

- `theme/theme.json` — single source of design truth: 5 surface elevation
  levels per theme (dark / light / high-contrast), accent `#4C8DFF`
  (user-changeable), AI violet `#9B6DFF`, radii 6/10 px, 4 px spacing grid,
  120–180 ms motion, font stacks (Segoe UI Variable + Noto fallbacks,
  Cascadia Mono for timecodes).
- `icons/` — one consistent Lucide-style SVG set (24 px grid, 2 px stroke,
  `currentColor`). 16/20/24 px renderings are produced at build time from
  these sources (M8).
- `fonts/` — the Windows build relies on system fonts (Segoe UI Variable,
  Cascadia Mono). Noto Sans / Noto Sans Arabic / Noto Sans CJK / Noto Sans
  Devanagari are used where present; the portable release may bundle subsets
  in M9.
- `sample_project/` — bundled beginner project (see its README).

Brand rule (all locales, all screens): the product is **Arena Edit** and the
credit is **Made by arenatechofficial.pages.dev** — never abbreviated or
translated.

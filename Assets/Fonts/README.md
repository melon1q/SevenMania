# Bundled fonts

- Michroma Regular: Google Fonts / googlefonts/Michroma-font. The official family provides weight 400 only; no fabricated bold file is included.
- Oxanium Regular and Bold: sevmeyer/oxanium, commit a8f39e0c71186190027a093e9001459410192d1e, fonts/ttf/.
- Both families are under SIL Open Font License 1.1. Full notices are in Licenses/ and are copied into runtime distributions.
- Google Fonts retrieval revision for Michroma and the license texts: b68b528a75da7d9b73f3928899c07a23083a6633.
- Main metadata: https://github.com/google/fonts/tree/main/ofl/michroma and https://github.com/google/fonts/tree/main/ofl/oxanium .

The game, editor and packaged defaults use Michroma in place of the retired proprietary HUD face. Legacy family names are translated on load. Oxanium and Oxanium Bold are available in the editor font selector. Private registration is process-local; no system font installation is performed.

Default skins that embed a font include its OFL text under licenses/. The skin loader accepts bounded UTF-8 .txt notices only in that directory. The game does not execute them.

SHA-256 (unmodified font files):
- Michroma-Regular.ttf: `b62301163788bc5b7f8fcac0b74b184e34e1827e577b499ecb724da065098f87`
- Oxanium-Bold.ttf: `15a44f661fd962c64cabe069a5f9f6fab85a412188f028d3e04da2ff917e37cc`
- Oxanium-Regular.ttf: `2b9c2a53ee0a8248ab901463838cc81075b410724046a09683b3cc91870fe52a`

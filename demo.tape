Output demo.gif

Set Width 1200
Set Height 700
Set FontSize 14
Set Theme "Dracula"
Set TypingSpeed 50ms
Set Shell "zsh"

# ──────────────────────────────────────────────────────────────────────────────
# Scene 1: 16 million rows — instant load, navigate, sort, column stats
# ──────────────────────────────────────────────────────────────────────────────

Type "csvview /Users/macbook/Downloads/estat_prc_hicp_ct_en.csv"
Enter
Sleep 5s

# Navigate to OBS_VALUE column (7 rights from start)
Right
Right
Right
Right
Right
Right
Right
Sleep 600ms

# Sort descending by OBS_VALUE
Type "]"
Sleep 3s

# Column statistics
Type "d"
Sleep 4s

# Close stats
Escape
Sleep 800ms

# Force quit cleanly
Ctrl+C
Sleep 2s

# Clear any leftover input
Ctrl+U
Sleep 400ms

# ──────────────────────────────────────────────────────────────────────────────
# Scene 2: Multi-series graph — 4 spectral lines vs redshift
# ──────────────────────────────────────────────────────────────────────────────

Type "csvview /tmp/cosmic_clean.csv"
Enter
Sleep 2s

# Navigate to CII_Tb (column 2 = 2 rights from z)
Right
Right
Sleep 400ms
Type "M"
Sleep 300ms

# Navigate to CO_10_Tb (column 6 = 4 rights)
Right
Right
Right
Right
Sleep 400ms
Type "M"
Sleep 300ms

# Navigate to CO_21_Tb (column 10)
Right
Right
Right
Right
Sleep 400ms
Type "M"
Sleep 300ms

# Navigate to CO_32_Tb (column 14)
Right
Right
Right
Right
Sleep 400ms
Type "M"
Sleep 400ms

# Open multi-series graph (all 4 marked columns)
Ctrl+G
Sleep 2s

# Switch to log scale inside graph mode
Type ":gy log"
Enter
Sleep 2500ms

# Move cursor to explore values
Right
Right
Right
Right
Right
Sleep 400ms
Right
Right
Right
Right
Right
Sleep 500ms

# Jump to global max
Type "M"
Sleep 2s

# Jump to global min
Type "m"
Sleep 2s

# Exit graph and quit
Escape
Sleep 500ms
Ctrl+C

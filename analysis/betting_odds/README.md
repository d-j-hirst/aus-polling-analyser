# Victorian seat betting odds

`capture_vic_2026.py` records every individual-seat price offered by Sportsbet and TAB, checks Betfair for exchange markets, and updates `analysis/seats/2026vic.txt` with the model-relevant prices.

Run it from the repository root:

```bash
python3 analysis/betting_odds/capture_vic_2026.py
```

Each run creates a timestamped Markdown file under `archive/2026vic`. These files are direct records of the displayed selections and odds, including Labor, Coalition, Nationals and One Nation. They do not contain calculated averages. Failed source captures are also recorded so that a missing site cannot look like a market withdrawal.

The script and local archive are gitignored. Each new archive is also copied to the configured private mirror. The script aborts before changing the seat file if that private copy cannot be created. Use `--no-private-mirror` only for testing.

The seat-file update follows these rules:

- Sportsbet and TAB are treated as fixed-odds bookmakers. When both quote the same included party in a seat, their decimal odds are averaged arithmetically.
- Greens map to `GRN`. Named independents and Victorian Socialists map to `IND` as independent or quasi-independent candidates.
- Labor, Liberal/Coalition/Nationals and One Nation prices are excluded.
- A party candidate's mean price is included only when it is $8.00 or shorter.
- Independent and quasi-independent prices are treated alike: they are included only when the seat has `bConfirmedProminentIndependent=1`, and are then included regardless of price. If that candidate has no specific selection with an agency, the agency's `Any Other` price is used. A specific selection takes precedence over `Any Other`.
- An unconfirmed independent or quasi-independent is omitted. If its mean price is $8.00 or shorter, the script prints a `REVIEW` notice so that the result can be raised for human assessment.
- The archive always retains the bookmaker's original selection wording, including `Any Other`.
- Betfair is treated as an exchange. Its best available back and lay prices are archived when individual-seat markets exist, but are never used in the seat file.
- The script owns the `sBettingOdds` entries in `2026vic.txt`. It refuses to update them if either fixed-odds bookmaker fails, while still writing an archive explaining the failure. `--allow-partial` is available for a deliberate partial update.

Useful options include `--archive-only`, and `--sportsbet-html`, `--tab-json`, or `--betfair-json` for processing previously saved responses. `--help` lists all options.

No public-repository workflow runs the capture because a complete odds archive should remain private. Schedule the command above locally once per day with the public repository root as the working directory. Only the filtered `sBettingOdds` changes belong in the public repository.

Sources checked for the initial implementation:

- [Sportsbet](https://www.sportsbet.com.au/betting/politics/vic-politics/victorian-state-election-seat-betting-10944986) and [TAB](https://www.tab.com.au/sports/betting/Politics/competitions/Victorian%20Politics) currently publish individual-seat markets.
- [Betfair](https://www.betfair.com/exchange/plus/en/politics/australia-victoria-state-politics-betting-12797648) currently publishes Victorian statewide markets but no individual-seat markets.
- [Oddschecker](https://www.oddschecker.com/politics/australian-politics/state-elections/victoria-state-election) currently shows a statewide Victorian election market, not individual seats.
- Searches of other major Australian bookmakers did not identify another current individual-seat market. They should be checked again as the election approaches.

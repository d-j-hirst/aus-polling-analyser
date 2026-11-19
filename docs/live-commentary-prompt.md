# What the live commentary prompt contains

This report measures how much information Australian Election Forecasts sends to its commentary writer and which details help explain a count. It uses the completed 2026 South Australian replay to compare prompt revisions 7 and 8.

Revision 8 reduced the median from **13,601 to 12,060 tokens** and the total from **2,001,730 to 1,786,308 tokens** across the same 146 successive updates. Those are measurements of the text prepared for the writer. The paid trials preceding this change used revision 6. Their receipts provide a separate baseline for comparing cost, speed and generated commentary.

Revision 8 implements the four compact forms described below. It also selects a single contest when reporting a district's changing TCP uncertainty. Across the replay, all **1,447 selected district ranges** contained both bounds for the same pair. In Adelaide's first counted update, for example, the writer can now compare the complete Labor–Liberal range at the two times. The earlier prompt supplied an upper Labor–Liberal bound and a lower Labor–One Nation bound.

The revised prompt averaged **1,908 tokens** for editorial instructions and format definitions, **2,081** for the election reference, **1,008** for the current overview and **7,238** for comparison-specific evidence. The instructions and election reference were byte-for-byte identical across all 146 requests. The previous-forecast benchmark uses each current overview once per model, so it requests caching only for the election prefix. Local checks reconstructed all **21,519** displayed level rows, including the **8,826** sent in their shorter form, and verified the sign of all **8,514** booth differences from expectations.

## Structure and size in revision 7

The worker sends editorial instructions first, followed by an election reference, the current forecast overview, and the evidence for one comparison period. The first two blocks can be identical throughout an election. The overview can be identical across several reports about the same forecast. The final block depends on both the forecast and its reference time.

The table uses **mean tokens per request** across the 146 comparisons with the previous forecast. Means allow the component sizes to add up. Percentages are shares of the entire input text. A token is a piece of text counted by the model's tokenizer. Some numbers, party identifiers and punctuation take several tokens.

| Part, in delivery order | Mean tokens | Share | What is vital or optional |
| --- | ---: | ---: | --- |
| Editorial instructions and guide to reading the figures | 1,854 | 13.5% | Keep the distinction between observed votes and estimates, the meaning of each pairing and unit, instructions for quiet updates, and the rules for quoting evidence. Examples and repeated style guidance are candidates for testing. Several address wording problems seen in earlier trials. |
| Election reference | 2,475 | 18.1% | Seat and candidate identities and party mappings are vital, particularly for multiple independents. Full names give context and the website can insert them from candidate placeholders. Seat holders and previous margins are useful when discussing incumbent prospects. The names and holder tables take about 1,436 and 875 tokens respectively and are reusable across updates. |
| Current all-seat overview | 1,008 | 7.4% | Keep an overview of all districts so the writer can see the wider election. It gives current win chances. A claim that a district *moved* still needs evidence from its comparison period. Precision in very safe seats is less important. |
| Forecast levels and movements | 3,680 | 26.8% | About 2,813 tokens describe selected districts and 864 describe the whole election. Keep win chances, plausible pairs, close margins and consequential parliamentary changes. An unchanged level can be stated once. Detailed statewide vote ranges are most useful when uncertainty changes. |
| Figures giving context to counts | 2,047 | 14.9% | Keep observations versus the previous booth expectation, correct candidates and count size. Category transfers, changes in counted leads and votes estimated to remain are essential when they explain an update. The signed gap and its magnitude plus direction describe the same comparison. |
| Selected observations and model diagnostics | 2,295 | 16.7% | Keep enough evidence to explain corrections, withdrawals, first counts for a replacement pair and movements from preference learning elsewhere. Candidate-by-candidate booth counts matter most for corrections or unusual allocations. Regional evidence, recovery details and manual samples earn their place when they explain a change. |
| Booth lists, column labels and comparison metadata | 352 | 2.6% | The booth lists average 164 tokens. Keep the reference time, unavailable evidence and input-revision flags so a change can be attributed to the right period. Internal forecast IDs are useful for audit and cheap to send. |
| **All input text** | **13,710** | **100%** | The 90th percentile was **14,836** tokens. |

The separate structured-output schema costs about **94 tokens** as compact JSON. It defines the summary, developments and evidence-insufficient flag. The table excludes that schema, API conversation framing and generated output. Subsections were tokenized individually, so punctuation and rounding can cause small differences between subtotals and totals.

Longer windows generally require more evidence. Across all **544 distinct comparisons** the median was **15,032** tokens and the 90th percentile was **16,045**. Median size by report was 13,601 for the previous forecast, 15,478 for 15 minutes, 16,559 for one hour, 14,036 for three hours, 14,399 for today, and 15,836 for since counting began. There were 567 applicable window labels. Windows with the same reference at the same forecast shared a request.

When the observed counts are known, an unchanged count or a revision below the configured ten-vote threshold gets local text. Yesterday's report reuses saved commentary. Neither operation needs a prompt. For larger updates, what matters can vary even when the prompt sizes are similar. Hurtle Vale needed both sides of its 2,986-vote category transfer and the district's net change. MacKillop needed the counted lead for the same pair and the shrinking estimated remainder. An April recheck with little forecast movement needed permission to give a one-sentence summary.

## Four ways to send the same evidence more compactly

These experiments changed the text supplied to the writer. Each check reconstructed the original displayed values or evidence fields, including units, signs, candidate identities and the distinction between missing and zero. The token counts include a short instruction explaining each proposed form. Generated prose was outside the test.

| Representation tested | Mean saving per previous-forecast request | What it preserves |
| --- | ---: | --- |
| Give the level once when the displayed before and after agree and the displayed change is zero | 564 | Of 21,510 level rows, 8,750 qualified. A change that rounds independently stays explicit whenever the displayed values differ. For instance, an expected seat total can go from 5.8 to 5.9 while its change rounds to +0.0. |
| Put candidate names, holder and margin under a single seat entry | 373 | All names, party IDs and margins. This saving is in the reusable election reference, so its billing benefit is smaller when the cache is warm. |
| Give the magnitude and direction of a booth's difference from expectation once | 303 | “3.7 pp below expectations” fully specifies the signed difference. The observed and expected shares and equivalent vote difference remain available. |
| Put fields identical at both observations under a common heading | 184 | The writer can still distinguish new, revised, withdrawn and unchanged candidate counts. Both versions are kept where values differ. The shorter form is used only when it reduces the serialized text length. |

Together the four changes reduced the 146 successive comparisons from **2,001,730 to 1,793,887 tokens (10.4%)**. Across all 544 distinct windows the reduction was **8,088,327 to 7,503,269 (7.2%)**. The previous-forecast median on election night went from 13,943 to 12,563; after election night it went from 13,337 to 11,802. These results establish the size saving. A writing trial would measure how reliably the model reads the more compact forms.

Two possible omissions carry a different trade-off. Removing *all* before/after candidate counts from the selected booths saves an average of **827 tokens** per successive update, but removes the evidence for who gained or lost votes in a correction. Simple new batches with a checked total and shares are a better candidate for conditional omission. Withholding the statewide 5th and 95th percentiles for FP and TPP while retaining their medians saves **248 tokens** on average. Both ends are valuable when the statewide range changes materially. These are separate omission tests and their savings overlap with the compact representations above.

## What the cache can reuse

The **4,329-token election prefix** was byte-identical in all 544 comparisons. The current overview was also identical for all windows of each forecast. Of 152 forecasts with at least one commentary request, 151 had multiple distinct comparisons. If each request after the first at a forecast found a warm overview, **397,096 overview tokens** would have been eligible for reuse across the replay. Simultaneous requests can all reach a cold cache, so this is a ceiling.

The earlier paid Sol 6.2 benchmark gives a useful comparison with actual behaviour. In that accelerated run, **145 of 146 requests** read **4,209 cached tokens** apiece. The first read none. Those receipts were for prompt revision 6. Natural count timing was very different. All 76 successive election-night gaps between SA forecasts with requests were under 30 minutes. After election night, only 4 of 74 were. The median post-count gap was about 64 minutes.

Two additional cache arrangements were evaluated against the SA schedule.

- **Move current statewide levels ahead of the comparison.** The 543 requests at multi-window forecasts could omit about 105,400 tokens of repeated current values from their comparison sections. Supplying a separately identifiable shared table added about 257,200 tokens across those requests. Even with one cache write and every sibling request hitting that write, the net saving at the configured write and read rates was only about **6,700 ordinary-input-token equivalents** over the whole replay, before any extra reading instructions. Keeping each level beside its change makes it easier to read.
- **Use a longer cache lifetime after election night.** At the SA source times, under one-writer-per-forecast assumptions, a 30-minute election prefix would need 71 cold writes across 75 post-count forecasts; a 12-hour prefix would need 11. With the trial's configured Terra Standard price of US$1 per million ordinary input tokens, a read at 0.05 times that rate, and writes at 1.25 and 2.5 times respectively, the difference is about **US$0.25 across the SA post-count**. This models only the election prefix, assumes a single model and successful reuse within each forecast, and uses the accounting rates in the repository. Actual cache hits, availability for a chosen deployment tier and latency were not measured.

The existing arrangement keeps changing comparison evidence last. Moving a daily or since-start reference ahead of the current overview would split the prefix across windows with different references. A repeated reference ID on its own does not make the selected seats, booths or their changes identical.

## A data improvement at the existing size

The revision 7 evidence selector reserved up to two rows per selected district for changes in the ends of its TCP uncertainty ranges. It ranked the endpoints separately. A 90% range needs the 5th and 95th percentiles for the **same two candidates**, at the same forecast.

Across the 146 successive comparisons, there were **1,732 district/pair occurrences** with at least one selected endpoint. **1,153** contained both ends and **579** contained only one. In all 579 cases the other endpoint existed in both saved forecasts. In 569 cases it had also changed. This affected **112 updates**. For example, in Adelaide at the first counted update the writer received the upper bound for Labor–Liberal and the lower bound for Labor–ON. Neither pairing had a complete interval in that prompt.

Revision 8 chooses the contest with the largest movement in either bound, weighted by the chance of that pairing at its more likely endpoint. Both bounds are supplied even when one stayed steady. A single bound may still be useful if no complete changed range is available, for example when the leading candidate's lower bound crosses 50%. The validator checks the pair and forecast behind a quoted range.

## How the measurements were made

The source was the saved `2026sa-commentary-full-20261118` replay. For each current/reference pair eligible for commentary, the benchmark rebuilt the evidence from the saved counts and summaries, preserving the archived comparison times, count activity and within-window reversals. The revision 7 measurements cover 544 distinct comparisons. The revision 8 measurements cover the 146 immediate comparisons also used in the paid model benchmark.

Token counts use `tiktoken` with `o200k_base` on the actual message text produced by `prepare_packet` and `input_messages`. Counts exclude JSON transport escaping. They are local measurements of prompt text and can differ from billed tokens. The four representation trials used the same tokenizer, forecast pairs and evidence. No forecast fitting, new API requests or publication was involved. [Evidence selection](../uploads/live_pipeline/comparisons.py) · [Prompt layout](../uploads/live_pipeline/prompt_layout.py) · [Benchmark and saved-window replay](../uploads/live_pipeline/benchmark.py)

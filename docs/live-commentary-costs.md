# Estimating live commentary costs

This note explains how capture frequency and commentary size affect the API budget for Australian Election Forecasts. It compares the available SA 2026 and Victorian 2022 archives without running forecasts or making paid API calls. The estimates help choose benchmark candidates; they do not measure model quality or latency.

## Full SA local publication

How much credit should be set aside to give each of the 153 SA captures all its public commentary windows? The previous full replay produced **544 distinct requests** after sharing identical comparisons and handling quiet counts locally. Its separate November 18 benchmark spent **US$1.56** on 146 Terra Standard/low requests for successive updates. The longer windows can contain more evidence than those successive updates.

The November 19 estimate rebuilds the commentary inputs for all 544 comparisons from the saved numerical evidence. It converts their text sizes to approximate input tokens using the measured Terra ratio of 0.327 tokens per UTF-8 byte and allows the measured mean of 445 output tokens per response. The night estimate allows reuse of the election reference and charges for writing the current forecast overview when several windows use it. The post-count estimate assumes fresh cache writes on every request. Both use the 30-minute cache prices for Terra Standard. [OpenAI API pricing](https://developers.openai.com/api/docs/pricing)

| Source time | Distinct requests | Estimated US dollars |
|---|---:|---:|
| Election night through the 6 am cutoff | 298 | 3.76 |
| Rest of the count | 246 | 4.19 |
| **Whole replay, calculated before rounding** | **544** | **7.94** |

If every request starts with a cold cache, the estimate is **US$9.40**. The full SA preset sets a **US$10** spending limit for the run. Actual response length, new forecast evidence, API failures and cache reuse can change expenditure. The worker reserves the maximum request cost before dispatch and checks the cumulative ledger on restart. At the limit it can still publish numerical results and mechanical comparisons after the commentary deadline. [Full SA preset](../uploads/sa-full-local-replay-commentary.json)

## Schedule estimate from the November 18 benchmark

This estimate uses the recorded low-reasoning requests from the second and third updates of the four-capture SA trial. Those requests averaged about 19,740 input tokens. The proposed Victorian schedule uses Sol Ultrafast between **7 pm and 9:30 pm AEDT**, Terra Fast for the rest of election night until the next morning's **6 am** cutoff, and Terra Standard afterwards. All use low reasoning. This is a cost scenario, not an enabled deployment configuration.

The workload was recalculated from the archive using the implemented reporting windows: previous forecast and since counting began, plus fifteen minutes/one hour on election night; afterwards, Today from 5 am Sydney time and a three-hour report starting at 1 pm. Yesterday's saved report requires no request. Comparisons sharing a reference within one forecast are counted once. Empty comparisons and changes affecting fewer than ten ballots receive local text. Cumulative activity includes corrections and restorations rather than relying only on endpoint differences.

The following unit costs use measured response lengths and configured benchmark prices. Night requests use the observed cache reads. Post-count requests assume **no cache hits**, including the cost of fresh writes under the current cache configuration. The latter assumption also applies when an accelerated replay might actually keep the cache warm.

| Configuration, all low reasoning | Night request with observed cache reuse | Request without cache hits |
| --- | ---: | ---: |
| Terra Standard | $0.01852 | $0.02353 |
| Terra Fast | $0.03732 | $0.04734 |
| Sol Standard | $0.03746 | $0.04748 |
| Sol Ultrafast | $0.22819 | $0.28836 |

Victoria's archive has **30 captures during the peak period**, producing 119 distinct commentary requests. The complete archive covers 26 November–12 December 2022. Every supplied capture is assumed to produce a forecast; processing delays may reduce the workload. The smaller schedule supplies only the 468 retained data-changing captures. The full schedule also includes repeated counts with later observation times, for 1,442 captures. Raw feed activity is an approximation to accepted/recovered forecast evidence; these calculations do not run the election model or include retries.

| Phase and selected model | Requests, data-changing captures | Cost | Requests, all captures | Cost |
| --- | ---: | ---: | ---: | ---: |
| 7–9:30 pm: Sol Ultrafast | 119 | $27.15 | 119 | $27.15 |
| Other election-night times: Terra Fast | 249 | $9.29 | 314 | $11.72 |
| After 6 am: Terra Standard, no cache hits | 1,305 | $30.71 | 3,220 | $75.78 |
| **Total** | **1,673** | **$67.16** | **3,653** | **$114.65** |

The full schedule is the more appropriate starting point for an unattended feed watcher. An unchanged immediate count needs no LLM response, but its since-start or daily comparison may still contain substantial counting. The unit-price extrapolation uses SA-sized prompts; Victoria has more districts, and later or longer-window explanations may require more input. Adding 10,000 uncached input tokens to every request raises these totals to about **$99.47** and **$167.41**, respectively. Output lengths, missing cache hits on election night and retries would also affect cost. A roughly **$150–175 planning allowance**, with a separate $200 spending limit, leaves more room than treating $114.65 as a firm quote.

For a full **2026 SA two-model comparison**, the retained archive contains 153 genuine captures from 21 March to 1 April. Six immediate comparisons are empty or below the ten-vote threshold, leaving 147 paid snapshot comparisons. Each comparison is sent once to Terra Standard/low and once to Sol Standard/low. This is distinct from generating every public reporting window for both models:

| SA test scope | Snapshot/window comparisons | Total requests across both models | Estimated cost | With no cache hits anywhere |
| --- | ---: | ---: | ---: | ---: |
| Previous-update comparison only | 147 | 294 | $9.28 | $10.44 |
| All distinct reporting windows | 545 | 1,090 | $34.22 | $38.71 |

Both estimates already assume no post-count cache hits. An extra 10,000 uncached input tokens per request raises the estimates to approximately $13.69 and $50.57. The [prepared full replay](live-automation.md#full-sa-two-model-commentary-check) now selects the previous-update test with an explicit **$15 aggregate limit**. The small trial still uses $10. Testing all windows would need a separate $40–50 allowance and is not enabled. No full paid run was launched to obtain these estimates or prepare the project.

## Counting requests

The audit tables below use the **earlier reporting schedule**: previous forecast, since-start, and either fifteen minutes/one hour on election night or three/twenty-four hours afterwards. Windows sharing the same reference require only one request, and unchanged counts or movements below ten votes receive local text.

The implemented schedule now replaces twenty-four hours with **Today** from 5 am Sydney time and schedules the three-hour report only from 1 pm onwards. Yesterday's saved report requires no new call. Morning post-count forecasts therefore have at most three distinct new requests rather than four. The archive counts and dollar scenarios below have **not** been recalculated for this schedule or the smaller cached prompts; they describe the earlier workload, not a current-run quote.

The following audit compares raw candidate counts, including declaration accounts, rather than running accepted-count recovery and the forecast. It assumes every supplied capture finishes a forecast before the next one, no retries, and no additional reuse across different forecasts. Real forecasting delays can reduce the number of forecasts; corrections and recovery can alter the activity decisions. These are workload scenarios, not guaranteed request counts.

| Input schedule | Captures | Requests before the 6 am cutoff | Later requests | Total requests |
| --- | ---: | ---: | ---: | ---: |
| SA 2026 retained captures, 21 March–1 April | 153 | 298 | 273 | 571 |
| Victoria 2022 data-changing captures, 26 November–11 December | 468 | 368 | 1,369 | 1,737 |
| Victoria 2022 all captures, 26 November–12 December | 1,442 | 433 | 3,594 | 4,027 |

The Victorian archive includes 974 repeated-data captures. Its full schedule is relevant because a new observation time can still warrant a forecast when votes are unchanged. A quiet immediate comparison avoids an API call, but its since-start and longer-window comparisons can still contain substantial counting. Suppressing those forecasts altogether would be a separate operating policy, not what the automated pipeline currently does.

## Prices and illustrative totals

[OpenAI's published pricing](https://developers.openai.com/api/docs/pricing) was checked on **17 November 2026**. These are US dollars per million tokens at short-context rates. Astra's current API identifier is `gpt-6-astra`; Terra and Sol are `gpt-6.1-terra` and `gpt-6.1-sol`.

| Model and speed | Input | Cached input | Output |
| --- | ---: | ---: | ---: |
| Terra Standard | $1 | $0.05 | $5 |
| Terra Fast | $2 | $0.10 | $10 |
| Sol Standard | $2 | $0.10 | $10 |
| Sol Fast | $4 | $0.20 | $20 |
| Sol Ultrafast | $12 | $0.60 | $60 |
| Astra Standard | $10 | $1 | $50 |
| Astra Fast | $20 | $2 | $100 |

For a common comparison, assume **30,000 uncached input tokens and 2,000 output tokens per request**, including reasoning. This is a planning assumption, not measured token usage. Before the compact fact-table layout, saved first-results SA evidence produced an approximately 81 kB prompt including instructions and structured-output definitions; two inspected Victorian comparisons were approximately 115–118 kB. The current layout removes duplicated quantities and repeated units. JSON bytes are not tokens. Actual token use, caching, retry rates and reasoning consumption must be measured in the benchmark. The tables below retain their original uncached-volume assumption for comparison, rather than treating smaller prompts as measured token savings.

For example, Sol Standard costs `(30,000 × $2 + 2,000 × $10) / 1,000,000 = $0.08` per request under that assumption. The totals below are rounded dollars and assume the named speed for the entire count.

| Model and speed | SA, 571 requests | Victoria, 1,737 requests | Victoria, 4,027 requests |
| --- | ---: | ---: | ---: |
| Terra Standard | $23 | $69 | $161 |
| Terra Fast | $46 | $139 | $322 |
| Sol Standard | $46 | $139 | $322 |
| Sol Fast | $91 | $278 | $644 |
| Sol Ultrafast | $274 | $834 | $1,933 |
| Astra Standard | $228 | $695 | $1,611 |
| Astra Fast | $457 | $1,390 | $3,222 |

If input use is 20,000–40,000 tokens instead, with output still 2,000, the full Victorian schedule costs about **$121–201 for Terra Standard** or **$242–403 for Sol Standard** before retries. Thus a $200 ceiling is plausible with Terra but cannot yet be promised. A smaller prompt, fewer completed forecasts, caching or a lower commentary frequency could materially change that conclusion.

Using premium speed only before the 6 am cutoff is cheaper than using it throughout. Terra Fast followed by Terra Standard is about **$178** for the full Victorian schedule. Sol Fast followed by Sol Standard is about **$357**, or **$168** on the data-changing schedule. Sol Ultrafast followed by Sol Standard is about **$496**, or **$286** on the data-changing schedule. These are scenarios; a deployment model and speed schedule have not been enabled.

Using Ultrafast for only a selected peak period is a different question. Under the same uncached assumption, replacing one Sol Fast request with Ultrafast adds **$0.32**. A hundred captures can require up to four distinct commentary requests each, so that peak-period premium could be up to **$128**, rather than $32 for a hundred requests. Unchanged counts, shared comparison anchors and cache reuse can reduce it. This supports measuring Ultrafast's latency in the trial without selecting it for the whole count.

The current implementation uses explicit cache boundaries. A 30-minute cache write costs 1.25 times ordinary input; a cache read uses the discounted input rate. A written token is not also charged as ordinary input. During regular reporting the changing comparison suffix is left uncached; the benchmark also leaves it uncached. Consecutive updates test reuse of the unchanged election context, not of the whole prompt. These effects are absent from the illustrative uncached tables above. Actual receipts, including reasoning tokens, will inform any deployment schedule.

## Reproducing the small comparison

Use the four-capture replay and free dry-run command in [the operator guide](live-automation.md#four-capture-sa-commentary-check). `uploads/sa-commentary-benchmark.json` selects six model/speed choices, each at low/medium reasoning: Terra Standard/Fast, Sol Standard/Fast/Ultrafast and Astra Standard. The four-capture trial makes 36 attempts: twelve configurations on each of three consecutive comparisons. Each configuration processes its updates consecutively to measure reuse of the election context. The previous two-comparison trial cost about $2.93, including $1.18 for its cached second comparison. A third similarly sized comparison would suggest roughly $4.10 overall; allow about $4-5 for changed evidence and response lengths. This is an estimate from observed usage, not a fixed quote. The aggregate benchmark ceiling is US$10. Pass `--confirm-paid` to the benchmark command to authorise those requests.

Assess explanation quality and factual grounding alongside request latency, cache reads/writes and deadline compliance. The expensive modes need a limited period or frequency under a $200 budget; a stronger-model reference can still be affordable if deliberately selected. A successful structured response is not proof that its explanation is correct. Benchmark requests receive their own measurement windows; normal publication still includes queue delay in its deadline.

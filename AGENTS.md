## Resource-intensive operations

Do not run Stan models or other full Bayesian fitting jobs unless the user
explicitly asks you to do so.

Do not build or run the wxWidgets/C++ computation application unless the
user explicitly asks.

Prefer pipeline dry-run/test options when validating Python changes.

## Native Windows Python

Use `analysis/.venv-win/Scripts/python.exe` for non-Stan work on Windows.
The PATH Python commands may be unconfigured pyenv shims. Keep the existing
Linux `analysis/env` separate; do not use it from a native Windows shell.

From the repository root, run routine tests with:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\tests\run_tests.py
```

For pipeline inspection, use `analysis/` as the working directory:

```powershell
.\.venv-win\Scripts\python.exe -B pipeline.py status --election 2026vic
.\.venv-win\Scripts\python.exe -B pipeline.py plan --election 2026vic --profile regular
```

Status and all planning profiles are non-Stan. Generation profiles can launch
Stan even when named `regular`; `run` defaults to calibration. The routine
test selection excludes real Stan fitting; do not use `--all` in this environment.
Setup and recreation instructions are in `analysis/README.md`.

## Documentation

Refer to the website as "Australian Election Forecasts" or "aeforecasts.com".

Public documentation explains implemented behaviour, reproduction commands,
source evidence and consolidated findings for observers and contributors.
Start each public document with its purpose, then explain its function in
plain language before giving commands or technical details.

For public and internal reports, explain the question and purpose of each
analysis before presenting its results. Define terms, comparison units,
model names, columns and denominators; use a small example when helpful.
Group like measurements and comparisons together, and prefer plain descriptions
such as "test election" to opaque terminology such as "fold".

Keep internal plans, priorities, delivery sequences and proposed work under
the gitignored `docs/planning/` directory. Public documents and generated
public reports must not link to or reproduce that planning material.

## Code style and structure

Maintain a clear distinction between different levels of abstraction. For any
given process, a top-level function should outline its overall flow, with the
details of each substantial stage handled by a separate function where possible.

Use names that identify electoral quantities and conceptual steps. Split dense
expressions into named intermediate quantities when this helps readers follow
the reasoning. Comments should complement this structure. Avoid trivial helper
functions or repetitive descriptions of obvious operations introduced solely to
make the code appear more readable.

Comments should be used extensively to establish context for a piece of code
and to explain decisions made. Write for the audience defined in the Commenting
section below, so readers can understand the purpose and conceptual flow without
following the algorithmic details, language-specific syntax or libraries.

When making a fix or adjusting a piece of code to account for an atypical
situation or specific case, ensure that the more common flow is also mentioned
among comments so that the atypical situation is not given undue weight. Without
this, a reader not familiar with the code's broader context and intentions may
incorrectly conclude that the entire piece of code is about the specific case.

## Commenting

This is a publicly visible repository in a domain which receives a high degree of
public interest. As a result, maintaining the highest standards of transparency and
clarity is essential. This means that code comments should be used differently to
a typical code project: they must be expanded to make the flow, purpose and context
of the code comprehensible even to non-programmers and non-domain experts.

### Audience

We can expect those looking at the code to have the following abilities:

- High school mathematics, but rusty: a broad intuition for probabilities,
  averages and spread, and recognition of familiar statistical terms. For example,
  they recognise "normal distribution" and roughly picture its bell-shaped curve,
  but cannot be expected to recall its properties, formulas or procedures.
- Basic familiarity with fundamental programming structures, such as if/else blocks,
  for loops, etc.
- General familiarity with concepts and terminology relevant to Australian elections:
  key parties, turnout, formality, preferences and preference flows, FP/TCP/TPP votes,
  the Coalition, CPV/OPV and exhaustion.

Do *not* assume:

- That recognising a statistical term means knowing how to calculate with it or
  understanding its implications for this model.
- Knowledge or awareness of more advanced statistical techniques, or ability to
  perform non-trivial mathematical manipulations in general.
- Knowledge of intermediate-advanced programming, computer science or data analysis
  terminology.
- Understanding of specialised terms specifically relating to election analysis that
  would not be common in widespread discussion.

### Standard of reader understanding

Make the conceptual flow readable independently of the implementation: a reader
following the file introduction, function descriptions and block comments should
understand what question the code answers, what information it uses, the main
stages of the calculation, and how its results contribute to the forecast. They
should not need to decipher expressions to discover an important modelling decision.

Distinguish evidence from assumptions: a reader should be able to distinguish
what the data tells us, what the model assumes, and what the code does with each.
Assess the completeness of explanations by reader understanding, without imposing
comment counts or length targets.

### Purpose, inputs and outputs

Explain purpose and context: introduce functions, classes and distinct conceptual
blocks with a plain-English explanation of their role in the wider process. Describe
what the code is trying to achieve, rather than only listing its operations.

Describe the information crossing each boundary: explain all meaningful inputs and
returned results, files read or written, and shared state used or changed, in terms
of the information they represent. Include important information obtained through
other function calls, but do not enumerate every call or narrate the implementation.
Explain how the result is used by the next stage where that connection is not clear.

### Quantities and terminology

Define important quantities: explain the meaning of important inputs, intermediate
values and outputs, including their units, population, denominator and reference
point where relevant. State what one item represents, such as a poll, electorate,
counting category or simulated election. Distinguish percentages from proportions
and percentage-point changes, observed counts from estimated final counts, and
actual vote shares from transformed values used internally. Explain which party or
quantity gains when an adjustment is positive.

Introduce unfamiliar terms through their meaning: give a plain-language explanation
at first use in a relevant function or section, retaining the technical name where
it helps precision or further reading. A brief reminder or reference can suffice
when a shared concept has already been explained nearby. Avoid unexplained
abbreviations and statistical or programming jargon outside the audience's assumed
knowledge.

Explain techniques through their role and consequences: describe what a statistical
technique allows the model to do, why it is used here, and the assumptions it introduces.
For a transformation, explain why the calculation uses a different scale and how
its results relate back to the electoral quantity. Detailed derivations and syntax
explanations are not required; the conceptual mechanism and consequential assumptions
must still be understandable. Use a small numerical example when it clarifies a
distinction more effectively than additional terminology.

### Modelling choices, evidence and uncertainty

Make consequential choices visible: explain constants, thresholds, weights,
exclusions and defaults, including what they control, why they are needed, and
where their values come from. Distinguish values estimated from historical data,
externally supplied facts, modelling assumptions and safeguards against calculation
problems. Reference supporting public evidence or documentation where available.
Do not invent a rationale for inherited code; explicitly identify an unknown
rationale. Claims such as "improves accuracy" require evidence, rather than merely
describing an intention.

Explain assumptions and uncertainty in electoral terms: state what is treated as
known, estimated or uncertain, and what remains fixed while simulations vary other
quantities. Explain whether an adjustment changes the expected result, the range
of possible results, or both, where relevant. State which information was available
at the forecast date and which historical elections supplied estimates when this
matters to interpreting the calculation or its evaluation.

Explain missing data and exceptional behaviour: distinguish missing observations
from genuine zeros and unavailable voting categories. Describe consequential fallback
behaviour and its limitations alongside the ordinary case. Explain whether a safeguard
addresses a numerical problem, an input error or uncertainty about the election;
these are different reasons for changing or rejecting a value.

### Placement and maintenance

Place explanations at the appropriate level: put the overall purpose at file level,
a function's role and the information it receives and produces beside the function,
and specific decisions beside the relevant block. Explain shared concepts fully
in one maintained location, with a brief local reminder and a precise reference
elsewhere. A reference should supplement enough local explanation to understand
the code's purpose.

Keep comments consistent with behaviour: update affected comments whenever behaviour
changes, even if the code change is small. Describe the implemented behaviour and
its limitations accurately. Keep internal plans and proposed work in the planning
directory specified above; use public references for explanations intended for
repository readers.

### What to add expanded comments to

Apply the standard according to effect: add expanded comments to code on the path
from source data to forecast output, including calibration and historical analysis
that determine forecast parameters. GUI code that chooses model settings, or
prototype code whose results become production parameters, needs the same explanatory
standard for those decisions.

Keep routine infrastructure proportionate: GUI plumbing, automated tests, temporary
prototypes, provenance checking and build tools can use the sparser comment density
typical of a regular programming project when they do not make those analytical
decisions. A brief comment at the top of each file outlining its purpose is still
helpful, unless that would complicate parsing.

Limit expansion to the work in scope: add expanded comments for newly written code,
code you have heavily modified, or code where you have been explicitly instructed to
expand the comments. This does not excuse leaving affected comments inaccurate after
a smaller change, and does not require an unrelated sweep of existing code.

### Example of the intended explanatory style

Use concrete examples where helpful: the following illustrates how a comment can
explain a simulation step without requiring the reader to follow its algorithm.
Actual comments must describe the behaviour of the code they accompany.

```python
# The published candidate counts are held fixed while we vary how the
# remaining votes might divide between candidates. This represents uncertainty
# about votes whose candidate allocation is not yet known.
#
# For example, if 600 votes have been counted and this scenario expects 1,000
# in total, only the remaining 400 votes are redistributed here. The expected
# total is supplied by the turnout calculation; this step changes their
# allocation between candidates.
```

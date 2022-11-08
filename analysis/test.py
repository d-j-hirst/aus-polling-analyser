import sample_kurtosis
import statistics
import scipy.stats

elections = [
    # Federal
    48.47,
    49.64,
    46.51,
    50.12,
    52.7,
    47.26,
    # NSW
    47.98,
    45.68,
    35.78,
    52.26,
    #VIC
    57.3,
    51.99,
    48.42,
    54.39,
    #QLD
    53.2,
    51.3,
    51.1,
    37.2,
    50.9,
    55.0,
    #SA
    48.06,
    47,
    48.4,
    56.5,
    #WA
    55.5,
    42.71,
    48.15
]

old_elections = [
    # Federal
    48.97,
    50.98,
    46.37,
    51.44,
    49.9,
    50.86,
    51.77,
    53.23,
    49.6,
    45.4,
    44.3,
    51.7,
    52.7,
    50.2,
    43.1,
    47.4,
    50.5,
    45.9,
    45.8,
    50.7,
    49.3,
    # NSW
    56.13,
    55.96,
    48.82,
    47.31,
    44.04,
    52.4,
    58.7,
    60.7,
    #VIC
    57.78,
    50.2,
    46.53,
    43.7,
    49.49,
    50.7,
    53.8,
    49.5,
    44.2,
    44.8,
    45.8,
    41.6,
    41,
    42.1,
    42.2,
    42.1,
    56.7,
    #QLD
    46.73,
    53.7,
    53.8,
    46,
    46.6,
    #SA
    49.07,
    48.49,
    39.09,
    47.96,
    53.17,
    50.94,
    45,
    53.4,
    49.2,
    54.5,
    53.3,
    53.2,
    54.3,
    54.3,
    49.7,
    48.7,
    53,
    48.7,
    #WA
    52.28,
    52.92,
    44.84,
    44.55,
    47.62,
    54.12,
    53.74,
    49.03,
    45.3,
    49.83,
]

new_elections = [
    69.7,
    54.59,
    52.13
]

pre_wa_elections = elections + old_elections
all_elections = pre_wa_elections + new_elections

print(scipy.stats.describe(elections, bias=False))
print(scipy.stats.describe(pre_wa_elections, bias=False))
print(scipy.stats.describe(all_elections, bias=False))
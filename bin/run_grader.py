#! /usr/bin/env python3

import json
import os
import re
import scipy.stats
import subprocess
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

root = os.getcwd()
inputs_dir = Path(root, "inputs")
expected_dir = Path(root, "expected")

# Arg parsing
# Pass --fast multiple times to make it faster
n_trials = 500
for arg in sys.argv[1:]:
    if arg == "--fast":
        n_trials = n_trials // 10


# Utilities
# ======================================================================================
class bcolors:
    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"


test_results = []


def test_results_add(number, name, output, score, max_score=1):
    test_results.append(
        {
            "max_score": max_score,
            "name": name,
            "number": number,
            "output": output,
            "score": score,
        }
    )


def run_sim(args, inputfile):
    # Run the simulation.
    sim_process = subprocess.Popen(
        ["./cachesim", *args],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
    )

    # Pass in the input file on stdin.
    with open(inputfile, "rb") as i:
        stdout, _ = sim_process.communicate(i.read())

    # Return the lines that have OUTPUT at the beginning
    return list(filter(lambda l: l.startswith("OUTPUT"), stdout.decode().split("\n")))


# LRU and LRU_PREFER_CLEAN functionality
# ======================================================================================
print(f"{bcolors.BOLD}Checking LRU and LRU_PREFER_CLEAN functionality.{bcolors.ENDC}")

lru_i = 1
lru_prefer_clean_i = 1

# Iterate through all of the inputs, and for each of the corresponding "expected" files,
# run the simulation with the parameters specified by the expected fie.
for infile in sorted(inputs_dir.iterdir()):
    print(f"  Checking {infile}")
    for expected_file_path in sorted(expected_dir.glob(f"*-{infile.name}")):
        file_parts = re.match(
            rf"(lru|rand|lru_prefer_clean)-(\d+)-(\d+)-(\d+)-{infile.name}",
            expected_file_path.name,
        )
        replacement_policy, cache_size, cache_lines, associativity = file_parts.groups()
        print(
            f"    with parameters {' '.join(map(str.upper, file_parts.groups()))}...",
            end=" ",
        )

        # Calculate the test number and name
        test_number = {"lru": "1", "lru_prefer_clean": "3"}[replacement_policy]
        max_score = 1
        if replacement_policy == "lru":
            test_number += f".{lru_i}"
            lru_i += 1
            max_score = 2
        else:
            test_number += f".{lru_prefer_clean_i}"
            lru_prefer_clean_i += 1
        test_name = expected_file_path.name

        output_lines = run_sim(map(str.upper, file_parts.groups()), infile)

        # Get the expected output.
        with open(expected_file_path) as ef:
            expected_output_lines = [line.strip() for line in ef.readlines()]

        # Make sure everything matches up
        if len(output_lines) != len(expected_output_lines):
            print(f"{bcolors.BOLD}{bcolors.FAIL}FAIL{bcolors.ENDC}")
            error_text = "      {} OUTPUT lines found, expected {}".format(
                len(output_lines),
                len(expected_output_lines),
            )
            print(error_text)
            test_results_add(test_number, test_name, error_text, 0)
            continue

        # Check each of the output lines.
        fail = False
        for i, (found, expected) in enumerate(zip(output_lines, expected_output_lines)):
            if found != expected:
                print(f"{bcolors.BOLD}{bcolors.FAIL}FAIL{bcolors.ENDC}")
                error_text = "\n".join(
                    (
                        f"      On line {i} found:",
                        f"        {found}",
                        "      expected:",
                        f"        {expected}",
                    )
                )
                print(error_text)
                test_results_add(
                    test_number,
                    test_name,
                    error_text,
                    0,
                    max_score=max_score
                )
                fail = True
                break

        if fail:
            continue

        test_results_add(test_number, test_name, "PASS", max_score, max_score=max_score)
        print(f"{bcolors.BOLD}{bcolors.OKGREEN}PASS{bcolors.ENDC}")


# RAND functionality
# ======================================================================================
print(f"\n{bcolors.BOLD}Checking RAND functionality.{bcolors.ENDC}")

print(f"  Running {n_trials} trials. This may take a while.")
trials = []
hit_ratio_re = re.compile(r"OUTPUT HIT RATIO (\d+\.\d+)")
# https://stackoverflow.com/a/3160819/2319844
progressbar_width = 50
sys.stdout.write("    [%s]" % (" " * progressbar_width))
sys.stdout.flush()
sys.stdout.write("\b" * (progressbar_width + 1))  # return to start of line, after '['
for i in range(n_trials):
    output_lines = run_sim(
        ["RAND", "65536", "1024", "64"],
        inputs_dir.joinpath("trace1"),
    )
    for line in output_lines:
        match = hit_ratio_re.match(line)
        if match:
            trials.append(float(match.group(1)))

    if i % (n_trials / 50) == 0:
        sys.stdout.write("#")
        sys.stdout.flush()

sys.stdout.write("]\n")  # this ends the progress bar

print("  Checking that the code ran on all inputs successfully...", end=" ")
if len(trials) == n_trials:
    print(f"{bcolors.BOLD}{bcolors.OKGREEN}PASS{bcolors.ENDC}")
    test_results_add("2.1", "random trials success", "PASS", 5, max_score=5)
else:
    print(f"{bcolors.BOLD}{bcolors.FAIL}FAIL{bcolors.ENDC}")
    error_text = f"    {len(trials)} trials succeded, expected {n_trials}"
    print(error_text)
    test_results_add("2.1", "random trials failed", error_text, 0, max_score=5)

print("  Checking that the average is correct...", end=" ")
expected = 0.99430
average = sum(trials) / len(trials) if len(trials) > 0 else -1
epsilon = 0.00001
if abs(expected - average) < epsilon:
    print(f"{bcolors.BOLD}{bcolors.OKGREEN}PASS{bcolors.ENDC}")
    rand_trials_output = f"    average {average} is within {epsilon} of {expected}"
    rand_trials_score = 10
else:
    print(f"{bcolors.BOLD}{bcolors.FAIL}FAIL{bcolors.ENDC}")
    rand_trials_output = f"    average {average} not within {epsilon} of {expected}"
    rand_trials_score = 0
print(rand_trials_output)

test_results_add(
    "2.2",
    "random trials average",
    rand_trials_output,
    rand_trials_score,
    max_score=10,
)

print("  Checking that the distribution is normal...", end=" ")
# See this article for information about the interpretation of the Shapiro Wilk test
# result:
# https://towardsdatascience.com/6-ways-to-test-for-a-normal-distribution-which-one-to-use-9dcf47d8fa93?gi=28cc7af9338b#2efb
shapiro_test = scipy.stats.shapiro(trials) if len(trials) > 0 else None
if shapiro_test is None:
    print(f"{bcolors.BOLD}{bcolors.FAIL}FAIL{bcolors.ENDC}")
    normalcy_test_output = "    no trial data for Shapiro Wilk p-value test"
    normally_distributed_score = 0
else:
    shapiro_test_pvalue = (
        shapiro_test[1] if isinstance(shapiro_test, tuple) else shapiro_test.pvalue
    )
    if shapiro_test_pvalue >= 0.05:
        print(f"{bcolors.BOLD}{bcolors.OKGREEN}PASS{bcolors.ENDC}")
        normalcy_test_output = f"    Shapiro Wilk p-value {shapiro_test_pvalue} >= 0.05"
        normally_distributed_score = 5
    else:
        print(f"{bcolors.BOLD}{bcolors.FAIL}FAIL{bcolors.ENDC}")
        normalcy_test_output = f"    Shapiro Wilk p-value {shapiro_test_pvalue} < 0.05"
        normally_distributed_score = 0

# Print an actual histogram to the terminal and include it in the outut.
if len(trials) > 0:
    histogram = defaultdict(int)
    for t in trials:
        histogram[int(10 / (t * epsilon)) // 3] += 1
    min_bucket, max_bucket = min(histogram.keys()), max(histogram.keys())
    histogram_text = ""
    for row in range(max(histogram.values()), -1, -1):
        histogram_text += "    "
        for col in range(min_bucket, max_bucket + 1):
            histogram_text += "*" if histogram[col] >= row else " "
        histogram_text += "\n"

    normalcy_test_output += "\n\n" + histogram_text
    normalcy_test_output += (
        "    The above histogram should look somewhat like a normal distribution."
    )
print(normalcy_test_output)

test_results_add(
    "2.3",
    "histogram is normal",
    normalcy_test_output,
    normally_distributed_score,
    max_score=5,
)

if n_trials < 500:
    print(bcolors.WARNING)
    print("  NOTE: You are running the script with the --fast flag, so the above")
    print("  tests may fail at random. Your submission will be graded without")
    print("  --fast (which will perform 500 trials) which will give a much more")
    print("  accurate average and histogram. You can run the grader script without")
    print("  --fast by running:")
    print()
    print("     make grade-full" + bcolors.ENDC)


# Print out the test results and store to the test results JSON file.
# ======================================================================================
test_results_dir = Path(root, "test_results")
test_results_dir.mkdir(exist_ok=True, parents=True)
test_results_filename = test_results_dir.joinpath(
    datetime.now().strftime("%Y-%m-%d-%H-%M-%S.json")
)

# Sort by the number (convert to tuples of integers for sorting)
test_results.sort(key=lambda x: tuple(int(n) for n in x["number"].split(".")))
aggregated_scores = defaultdict(int)
aggregated_max_scores = defaultdict(int)
for tr in test_results:
    rubric_item_key = tuple(tr["number"].split(".")[0])
    aggregated_scores[rubric_item_key] += tr["score"]
    aggregated_max_scores[rubric_item_key] += tr["max_score"]

# Print out the results
print(f"\n{bcolors.BOLD}Results Summary{bcolors.ENDC}")
for k, v in aggregated_max_scores.items():
    print(f"  Rubric Item {'.'.join(k)}: {aggregated_scores[k]}/{v}")
print(bcolors.BOLD)
total_score = sum(aggregated_scores.values())
print(f"  Total Autograded Score: {total_score}/{sum(aggregated_max_scores.values())}")

print(bcolors.ENDC + bcolors.WARNING)
print("  NOTE: the starter code does not contain the complete set of test cases, so")
print("  this score may not fully represent your final autograded score.")
print(bcolors.ENDC)

# Write results to a file
print(f"{bcolors.BOLD}Writing results to {test_results_filename}{bcolors.ENDC}")
with open(test_results_filename, "w+") as f:
    json.dump(test_results, f)

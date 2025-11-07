# CSC 360: Operating Systems - p2

## Blake Stewart - v00966622

## Multi-Thread Scheduling (MTS)
- Spec Out: Oct 3, 2025 
- Design Due: Oct 17, 2025, 11:59 PM (Grade Received: 100%)
- Code Due: Oct 31, 2025, 11:59 PM

## Description
MTS is a multi-thread scheduler implemented in C. 
The scheduler emulates an automated control system managing trains crossing a single track.
The system must ensure that only one train is on the tracks at at time, while enforcing a strict set of priority and fairness rules.

## Design
In short, my design each train is represented as its own thread that simulates loading time, readiness, and crossing behaviour.
There is a central dispatcher thread that coordinates the scheduling of the trains based on their direction, priority and order of readiness.
To ensure sychronization, we utilizes mutexes and conditional variables.

Please refer to my design plan in "p2-A-blakestewart-V00966622.txt" for more details.

## Key Requirements

As per the assingment description (3.2 Simulation Rules)

The rules enforced by the automated control system are:

- Only one train is on the main track at any given time.
- Only loaded trains can cross the main track.
- If there are multiple loaded trains, the one with the high priority crosses.
- If two loaded trains have the same priority, then:
    - If they are both traveling in the same direction, the train which finished loading first gets the clearance to cross first. If they finished loading at the same time, the one that appeared first in the input file gets the clearance to cross first.
    - If they are traveling in opposite directions, pick the train which will travel in the direction opposite of which the last train to cross the main track traveled. If no trains have crossed the main track yet, the Westbound train has the priority.
- To avoid starvation, if there are two trains in the same direction traveling through the main track back to back, the trains waiting in the opposite direction get a chance to dispatch one train if any.

## Working Features
- Input Handling
- Mutual Exclusion
- Priority Enforcement
- Ready Order
- Fairness Rule
- Correct Timing
- Synchronization
- Output Format

## Installation
- Clone the repository
- Navigate to the project directory

## Testing

This submission includes a test input file name input.txt in the root directory of the p2 repository.
The format of the test input file follows the same format as defined in Section 3.11 of the assignment description.

#### To test:
Compile the source code:
- Use the Makefile (call make in terminal)
Run mts passing input file:
- ./mts input.txt

#### Why this input.txt?
This input was chosen as it covers:
- High vs Low Priority
- Same priority, same direction ties
- Opposite directions
- Fairness after 2 streak
- Loading differences

**The estimated execution time of mts with the provided test input file is 4 seconds.**

After the simulation is finished, the output results of mts is saved in the file named output.txt found in the root directory of the p2 repository.

## Requirements
- Linux operating system
- GCC compiler

## Authors and acknowledgment
- Blake Stewart
- UVIC CSC 360: Operating Systems

## Project status
- Complete.

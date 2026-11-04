//! Block placement from a profile (-fprofile-use).
//!
//! A function's blocks otherwise stay in source order, so a hot path jumps
//! over whatever was written beside it: sqlite3VdbeExec ran 177M `b`s, and
//! its hot opcodes sit among their own error paths. With counts, blocks are
//! laid out in traces: starting from the entry, then from the hottest block
//! not yet placed, each trace follows the hottest successor not yet placed,
//! unless that runs over twice as often: then most of its runs come from
//! elsewhere, and it is left for those (the prologue before a hot loop).
//! Blocks that never ran keep their source order, after everything else.
//!
//! A trace also reaches back: before starting at a block it starts at a
//! predecessor that has no other successor and runs about as often, if there
//! is one. Such a block is one piece of straight-line code with the seed, as
//! `pOp++` is with the switch it runs into. The backend copies that whole
//! piece into each case of an interpreter loop (see the AArch64 peephole
//! duplicate_switch_dispatch), which needs it to stay in one piece.
//!
//! This runs after the profile point, so that it does not change the blocks
//! a profile is matched against. Functions without counts are left alone.

use crate::backend::liveness::terminator_targets;
use crate::common::fx_hash::FxHashMap;
use crate::ir::reexports::{IrFunction, IrModule};

pub(crate) fn place_blocks(module: &mut IrModule) {
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        place_function(func);
    }
}

fn place_function(func: &mut IrFunction) {
    let Some(profile) = &func.profile else { return };
    let n = func.blocks.len();
    if n < 3 {
        return;
    }
    let index: FxHashMap<u32, usize> = func.blocks.iter().enumerate().map(|(i, b)| (b.label.0, i)).collect();
    let count: Vec<u64> = func.blocks.iter().map(|b| profile.get(&b.label.0).copied().unwrap_or(0)).collect();
    let targets: Vec<Vec<usize>> = func.blocks.iter()
        .map(|b| terminator_targets(&b.terminator).iter().filter_map(|t| index.get(t).copied()).collect())
        .collect();
    let order = trace_order(&count, &targets);
    let mut blocks: Vec<Option<_>> = std::mem::take(&mut func.blocks).into_iter().map(Some).collect();
    func.blocks = order.into_iter().filter_map(|i| blocks[i].take()).collect();
}

/// The order of blocks 0..n, block 0 first (see the module comment).
fn trace_order(count: &[u64], targets: &[Vec<usize>]) -> Vec<usize> {
    let n = count.len();
    let mut seeds: Vec<usize> = (1..n).filter(|&i| count[i] > 0).collect();
    seeds.sort_by_key(|&i| (std::cmp::Reverse(count[i]), i));
    // Blocks whose one successor is the key.
    let mut only_into: Vec<Vec<usize>> = vec![Vec::new(); n];
    for (i, t) in targets.iter().enumerate() {
        if let [t] = t[..] {
            only_into[t].push(i);
        }
    }
    let mut placed = vec![false; n];
    let mut order = Vec::with_capacity(n);
    for seed in std::iter::once(0).chain(seeds) {
        let mut cur = seed;
        let mut walked = vec![cur];
        // Nothing goes before the entry.
        while seed != 0 && !placed[cur] {
            let back = only_into[cur].iter().copied().filter(|&p| !placed[p] && !walked.contains(&p) && count[p] * 2 >= count[cur])
                .max_by_key(|&p| (count[p], std::cmp::Reverse(p)));
            let Some(p) = back else { break };
            walked.push(p);
            cur = p;
        }
        while !placed[cur] {
            placed[cur] = true;
            order.push(cur);
            let hottest = targets[cur].iter().copied().filter(|&t| !placed[t] && count[t] > 0 && count[t] <= count[cur].saturating_mul(2))
                .max_by_key(|&t| (count[t], std::cmp::Reverse(t)));
            match hottest {
                Some(t) => cur = t,
                None => break,
            }
        }
    }
    order.extend((0..n).filter(|&i| !placed[i]));
    order
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_trace_follows_the_hottest_successor() {
        // 0 -> 1 (rare) or 2; 1 -> 3; 2 -> 3; 3 returns; 4 never ran.
        let targets = vec![vec![1, 2], vec![3], vec![3], vec![], vec![3]];
        assert_eq!(trace_order(&[100, 1, 99, 100, 0], &targets), [0, 2, 3, 1, 4]);
    }

    #[test]
    fn test_blocks_that_never_ran_keep_their_order_at_the_end() {
        let targets = vec![vec![3, 1], vec![2], vec![], vec![]];
        assert_eq!(trace_order(&[5, 0, 0, 5], &targets), [0, 3, 1, 2]);
    }

    #[test]
    fn test_the_entry_stays_first_though_a_loop_is_hotter() {
        // 0 -> 1 -> 2 -> 1 or 3.
        let targets = vec![vec![1], vec![2], vec![1, 3], vec![]];
        assert_eq!(trace_order(&[1, 1000, 1000, 1], &targets), [0, 1, 2, 3]);
    }

    #[test]
    fn test_a_trace_starts_at_the_straight_line_code_before_its_seed() {
        // The hottest block 3 is a switch; 2 runs straight into it, and the
        // cases 4 and 5 go back to 2.
        let targets = vec![vec![1], vec![3], vec![3], vec![4, 5], vec![2], vec![2], vec![]];
        assert_eq!(trace_order(&[1, 1, 100, 100, 45, 45, 0], &targets), [0, 1, 2, 3, 4, 5, 6]);
        // A case that runs most of the time may run straight into it too.
        assert_eq!(trace_order(&[1, 1, 100, 100, 60, 40, 0], &targets), [0, 1, 4, 2, 3, 5, 6]);
        // A predecessor that runs far less often is not part of the seed.
        let targets = vec![vec![2], vec![3], vec![3, 4], vec![], vec![]];
        assert_eq!(trace_order(&[1, 1, 1, 100, 1], &targets), [0, 2, 4, 3, 1]);
        // A cycle of single successors ends the walk.
        let targets = vec![vec![], vec![2], vec![3], vec![1]];
        assert_eq!(trace_order(&[1, 5, 5, 5], &targets).len(), 4);
    }

    #[test]
    fn test_a_trace_leaves_a_block_mostly_reached_from_elsewhere() {
        // 0 and 2 both go to the loop head 1, which 2 runs into 99 times of
        // 100: 2 goes before 1, though 0 is placed first.
        let targets = vec![vec![1], vec![3, 4], vec![1], vec![2], vec![]];
        assert_eq!(trace_order(&[1, 100, 99, 99, 1], &targets), [0, 3, 2, 1, 4]);
    }

    #[test]
    fn test_the_entry_stays_first_with_a_block_running_into_it() {
        // 1 has only the entry as a successor, and runs as often.
        let targets = vec![vec![1], vec![0]];
        assert_eq!(trace_order(&[5, 5], &targets), [0, 1]);
    }

    #[test]
    fn test_every_block_is_placed_once() {
        let targets = vec![vec![1, 2], vec![0], vec![2], vec![0, 1, 2, 3]];
        let mut order = trace_order(&[0, 7, 7, 7], &targets);
        assert_eq!(order[0], 0);
        order.sort_unstable();
        assert_eq!(order, [0, 1, 2, 3]);
    }
}

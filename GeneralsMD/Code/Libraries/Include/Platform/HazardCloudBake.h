// Device-free bounded slice traversal for shared hazard cloud snapshots.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

namespace HazardCloudShared {

class BakeSlices {
public:
	void begin(unsigned slices, unsigned perFrame)
	{
		total = slices;
		completed = 0;
		// Each residue class contains at most perFrame slices. Visiting one
		// class per render spreads its work over the volume's full height.
		stride = slices && perFrame ? 1 + (slices - 1) / perFrame : 1;
	}
	void cancel() { total = completed = 0; stride = 1; }
	unsigned remaining() const { return total - completed; }
	bool complete() const { return completed == total; }
	unsigned next()
	{
		if (complete()) return total;
		unsigned ordinal = completed++;
		for (unsigned residue = 0; residue < stride; ++residue) {
			const unsigned count = residue < total ? 1 + (total - 1 - residue) / stride : 0;
			if (ordinal < count) return residue + ordinal * stride;
			ordinal -= count;
		}
		return total; // Unreachable for an unfinished traversal.
	}

private:
	unsigned total = 0, completed = 0, stride = 1;
};

} // namespace HazardCloudShared

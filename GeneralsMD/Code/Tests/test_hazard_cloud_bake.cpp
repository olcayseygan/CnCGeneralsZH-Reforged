// No renderer or device: exercise every supported slice count and work bound.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "test_harness.h"
#include "Platform/HazardCloudBake.h"
#include <vector>

TEST(shared_cloud_bakes_visit_every_slice_with_bounded_work)
{
	for (unsigned slices = 6; slices <= 96; slices += 6) {
		for (unsigned budget = 1; budget <= 100; ++budget) {
			HazardCloudShared::BakeSlices progress;
			progress.begin(slices, budget);
			std::vector<bool> seen(slices, false);
			unsigned renders = 0, written = 0;
			while (!progress.complete()) {
				unsigned thisFrame = 0;
				while (!progress.complete() && thisFrame < budget) {
					const unsigned slice = progress.next();
					CHECK(slice < slices && !seen[slice]);
					seen[slice] = true;
					++thisFrame; ++written;
				}
				CHECK(thisFrame <= budget);
				CHECK(progress.remaining() == slices - written);
				++renders;
			}
			CHECK(written == slices && renders == (slices + budget - 1) / budget);
			CHECK(progress.next() == slices);
			progress.begin(slices, budget);
			progress.next();
			progress.cancel();
			CHECK(progress.complete() && progress.remaining() == 0);
			progress.begin(slices, budget);
			CHECK(progress.next() == 0); // A new generation discards partial progress.
		}
	}
	HazardCloudShared::BakeSlices progress;
	progress.begin(24, 8);
	for (unsigned i = 0; i < 8; ++i) CHECK(progress.next() == i * 3);
	for (unsigned i = 0; i < 8; ++i) CHECK(progress.next() == i * 3 + 1);
	for (unsigned i = 0; i < 8; ++i) CHECK(progress.next() == i * 3 + 2);
	CHECK(progress.complete());
}

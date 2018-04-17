#pragma once

#include <atlas/atlas_client.h>
#include <atlas/meter/monotonic_counter.h>

std::shared_ptr<atlas::meter::Gauge<double>> gauge(
    atlas::meter::Registry*, std::string name,
    const atlas::meter::Tags& tags = atlas::meter::kEmptyTags);

std::unique_ptr<atlas::meter::MonotonicCounter> monotonic_counter(
    atlas::meter::Registry*, const char* name,
    const atlas::meter::Tags& tags = atlas::meter::kEmptyTags);

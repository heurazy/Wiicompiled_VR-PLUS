// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

namespace mkw::vr {

// Connected position indices in an MDL0 array. Material/normal/UV seams do not
// split a component; disconnected rim, spokes, column and chassis pieces do.
class NativeWheelTopology {
  public:
    explicit NativeWheelTopology(size_t count) : parents(count), used(count, false), rootOwned(count, true) {
        std::iota(parents.begin(), parents.end(), 0u);
    }

    uint32_t Root(uint32_t i) {
        while (parents[i] != i) {
            parents[i] = parents[parents[i]];
            i = parents[i];
        }
        return i;
    }

    bool Triangle(uint32_t a, uint32_t b, uint32_t c) {
        if (a >= parents.size() || b >= parents.size() || c >= parents.size())
            return false;
        // Degenerate strip connectors must not join disconnected pieces.
        if (a == b || b == c || a == c)
            return true;
        used[a] = used[b] = used[c] = true;
        const auto root = Root(a);
        parents[Root(b)] = root;
        parents[Root(c)] = root;
        return true;
    }

    // MDL0 shape primitive data uses the shape's CP VCD. Unsupported direct
    // attributes/commands fail closed instead of guessing a vertex stride.
    bool AddPrimitives(const uint8_t *data, size_t size, uint32_t vcdLo, uint32_t vcdHi, uint32_t fixedNode = 0) {
        std::array<uint32_t, 10> nodes;
        nodes.fill(UINT32_MAX);
        uint32_t positionOffset = 0;
        for (unsigned bit = 0; bit < 9; ++bit)
            positionOffset += (vcdLo >> bit) & 1u;
        const uint32_t positionType = (vcdLo >> 9) & 3u;
        if (positionType < 2)
            return false;
        uint32_t stride = positionOffset;
        for (unsigned attr = 0; attr < 12; ++attr) {
            const uint32_t type = attr < 4 ? (vcdLo >> (9 + attr * 2)) & 3u : (vcdHi >> ((attr - 4) * 2)) & 3u;
            if (type == 1)
                return false;
            if (type)
                stride += type - 1;
        }
        const auto read16 = [](const uint8_t *p) { return (uint32_t(p[0]) << 8) | p[1]; };
        size_t at = 0;
        while (at < size) {
            const uint8_t command = data[at++];
            if (!command)
                continue;
            if (command == 0x20 || command == 0x28 || command == 0x30 || command == 0x38) {
                if (size - at < 4)
                    return false;
                if (command == 0x20) {
                    const auto address = read16(data + at + 2) & 0xfffu;
                    if (address % 12 || address / 12 >= nodes.size())
                        return false;
                    nodes[address / 12] = read16(data + at);
                }
                at += 4;
                continue;
            }
            const auto primitive = command & 0xf8;
            if (primitive != 0x80 && primitive != 0x90 && primitive != 0x98 && primitive != 0xa0)
                return false;
            if (size - at < 2)
                return false;
            const uint32_t count = read16(data + at);
            at += 2;
            if (count > (size - at) / stride || count < 3 || (primitive == 0x80 && count % 4) ||
                (primitive == 0x90 && count % 3))
                return false;
            const auto index = [&](uint32_t i) {
                const auto *p = data + at + size_t(i) * stride + positionOffset;
                return positionType == 2 ? uint32_t(*p) : read16(p);
            };
            for (uint32_t i = 0; i < count; ++i) {
                if (index(i) >= parents.size())
                    return false;
                uint32_t node = fixedNode;
                if (vcdLo & 1u) {
                    const uint32_t selector = data[at + size_t(i) * stride];
                    if (selector % 3 || selector / 3 >= nodes.size())
                        return false;
                    node = nodes[selector / 3];
                }
                if (node != 0)
                    rootOwned[index(i)] = false;
            }
            if (primitive == 0x80) {
                for (uint32_t i = 0; i < count; i += 4) {
                    if (!Triangle(index(i), index(i + 1), index(i + 2)) ||
                        !Triangle(index(i), index(i + 2), index(i + 3)))
                        return false;
                }
            } else if (primitive == 0x90) {
                for (uint32_t i = 0; i < count; i += 3)
                    if (!Triangle(index(i), index(i + 1), index(i + 2)))
                        return false;
            } else {
                for (uint32_t i = 2; i < count; ++i)
                    if (!Triangle(index(primitive == 0xa0 ? 0 : i - 2), index(i - 1), index(i)))
                        return false;
            }
            at += size_t(count) * stride;
        }
        return true;
    }

    std::vector<uint32_t> parents;
    std::vector<bool> used;
    std::vector<bool> rootOwned;
};

// MDL0 v8/9 have the shape dictionary at 0x30; v10/11 insert two fur
// dictionaries before it. All offsets below are checked within the MDL0.
inline bool ReadNativeWheelTopology(const uint8_t *mdl, size_t size, uint32_t arrayId, NativeWheelTopology &topology) {
    if (!mdl || size < 0x40)
        return false;
    const auto read32 = [&](size_t at) {
        return (uint32_t(mdl[at]) << 24) | (uint32_t(mdl[at + 1]) << 16) | (uint32_t(mdl[at + 2]) << 8) | mdl[at + 3];
    };
    const auto contains = [&](size_t at, size_t length) { return at <= size && length <= size - at; };
    const auto version = read32(8);
    if (read32(0) != 0x4d444c30 || version < 8 || version > 11 || read32(4) != size)
        return false;
    const size_t dictionary = read32(version >= 10 ? 0x38 : 0x30);
    if (!dictionary || !contains(dictionary, 8))
        return false;
    const auto count = read32(dictionary + 4);
    if (count > 4096 || !contains(dictionary + 8, size_t(count + 1) * 16))
        return false;
    bool found = false;
    for (uint32_t entry = 1; entry <= count; ++entry) {
        const size_t offset = read32(dictionary + 8 + entry * 16 + 12);
        if (offset > size - dictionary)
            return false;
        const size_t shape = dictionary + offset;
        if (!contains(shape, 0x60))
            return false;
        const auto positionId = (uint32_t(mdl[shape + 0x48]) << 8) | mdl[shape + 0x49];
        if (positionId != arrayId)
            continue;
        // NBT triplets can carry three normal indices; do not use the ordinary
        // one-index stride for them. Kart body shapes use XYZ normals.
        if (((read32(shape + 0x14) >> 2) & 3u) > 1)
            return false;
        const size_t group = shape + 0x24, dataOffset = read32(group + 8), length = read32(group + 4);
        if (dataOffset > size - group || !contains(group + dataOffset, length) || length > 0x400000)
            return false;
        if (!topology.AddPrimitives(mdl + group + dataOffset, length, read32(shape + 0x0c), read32(shape + 0x10),
                                    read32(shape + 8)))
            return false;
        found = true;
    }
    return found;
}

// The bone GX draws node `node` through: its 0x70 matrix is that bone's
// model-space transform, parents included. Found by node id, not dictionary
// position: the Flame Flyer and Cheep Charger bodies open with an nw4r_root
// bone on node 2 or 3, and the node-0 bone their wheel belongs to comes third.
inline bool ReadNativeWheelNodeMatrix(const uint8_t *mdl, size_t size, uint32_t node, float out[12]) {
    if (!mdl || size < 0x40)
        return false;
    const auto read32 = [&](size_t at) {
        return (uint32_t(mdl[at]) << 24) | (uint32_t(mdl[at + 1]) << 16) | (uint32_t(mdl[at + 2]) << 8) | mdl[at + 3];
    };
    const auto contains = [&](size_t at, size_t length) { return at <= size && length <= size - at; };
    const auto version = read32(8);
    if (read32(0) != 0x4d444c30 || version < 8 || version > 11 || read32(4) != size)
        return false;
    const size_t dictionary = read32(0x14);
    if (!dictionary || !contains(dictionary, 8))
        return false;
    const auto count = read32(dictionary + 4);
    if (!count || count > 4096 || !contains(dictionary + 8, size_t(count + 1) * 16))
        return false;
    for (uint32_t entry = 1; entry <= count; ++entry) {
        const size_t offset = read32(dictionary + 8 + entry * 16 + 12);
        if (offset > size - dictionary)
            return false;
        const size_t bone = dictionary + offset;
        if (!contains(bone, 0xa0))
            return false;
        if (read32(bone + 0x10) != node)
            continue;
        for (unsigned i = 0; i < 12; ++i) {
            const uint32_t bits = read32(bone + 0x70 + i * 4);
            float value;
            std::memcpy(&value, &bits, sizeof value);
            if (!std::isfinite(value))
                return false;
            out[i] = value;
        }
        return true;
    }
    return false;
}

} // namespace mkw::vr

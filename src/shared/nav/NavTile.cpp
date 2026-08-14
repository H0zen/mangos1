#include "nav/NavTile.hpp"

#include <algorithm>
#include <cmath>

namespace Nav
{
    void NavTile::Reset(int tileX, int tileY, float baseZ)
    {
        m_tileX = tileX;
        m_tileY = tileY;
        m_baseZ = baseZ;

        m_z.assign(size_t(CELLS_PER_TILE_SQ), 0);
        m_area.assign(size_t(CELLS_PER_TILE_SQ), PackArea(NavArea::Blocked, 0));
        m_clearance.assign(size_t(CELLS_PER_TILE_SQ), 0);
        m_region.assign(size_t(CELLS_PER_TILE_SQ), 0);

        m_stacked.clear();
        m_regions.clear();
        m_gateways.clear();
        m_gatewayCost.clear();
        m_links.clear();
    }

    void NavTile::SetCell(int inTile, uint16_t z, uint8_t area, uint8_t clearance,
                          uint16_t region)
    {
        m_z[size_t(inTile)] = z;
        m_area[size_t(inTile)] = area;
        m_clearance[size_t(inTile)] = clearance;
        m_region[size_t(inTile)] = region;
    }

    void NavTile::AddStacked(const StackedLayer& layer)
    {
        m_stacked.push_back(layer);
    }

    void NavTile::SortStacked()
    {
        std::sort(m_stacked.begin(), m_stacked.end(),
                  [](const StackedLayer& a, const StackedLayer& b)
                  {
                      return a.cell != b.cell ? a.cell < b.cell : a.z < b.z;
                  });
    }

    // The stacked table is sorted by cell, so one cell's extra surfaces are a
    // contiguous run. equal_range over a projection would need a comparator on a
    // half-built key; two lower_bounds on the cell index say the same thing plainly.
    void NavTile::StackedRange(int inTile, size_t& first, size_t& last) const
    {
        const uint32_t cell = uint32_t(inTile);

        const auto begin = std::lower_bound(m_stacked.begin(), m_stacked.end(), cell,
                                            [](const StackedLayer& l, uint32_t c)
                                            {
                                                return l.cell < c;
                                            });
        const auto end = std::lower_bound(begin, m_stacked.end(), cell + 1,
                                          [](const StackedLayer& l, uint32_t c)
                                          {
                                              return l.cell < c;
                                          });

        first = size_t(begin - m_stacked.begin());
        last = size_t(end - m_stacked.begin());
    }

    void NavTile::SurfacesAt(int inTile, std::vector<Surface>& out) const
    {
        out.clear();

        const uint8_t area = m_area[size_t(inTile)];
        if (!Nav::Walkable(area))
        {
            return;
        }

        Surface base;
        base.z = RestoreZ(m_z[size_t(inTile)], m_baseZ);
        base.region = m_region[size_t(inTile)];
        base.area = area;
        base.clearance = m_clearance[size_t(inTile)];
        base.layer = 0;
        out.push_back(base);

        if ((FlagsOf(area) & CELL_STACKED) == 0)
        {
            return;
        }

        size_t first = 0;
        size_t last = 0;
        StackedRange(inTile, first, last);

        for (size_t i = first; i < last; ++i)
        {
            const StackedLayer& l = m_stacked[i];

            Surface s;
            s.z = RestoreZ(l.z, m_baseZ);
            s.region = l.region;
            s.area = l.area;
            s.clearance = l.clearance;
            s.layer = uint16_t(i - first + 1);
            out.push_back(s);
        }
    }

    Surface NavTile::SurfaceAt(int inTile, uint16_t layer) const
    {
        const uint8_t area = m_area[size_t(inTile)];
        if (!Nav::Walkable(area))
        {
            return Surface();
        }

        if (layer == 0)
        {
            Surface s;
            s.z = RestoreZ(m_z[size_t(inTile)], m_baseZ);
            s.region = m_region[size_t(inTile)];
            s.area = area;
            s.clearance = m_clearance[size_t(inTile)];
            s.layer = 0;
            return s;
        }

        if ((FlagsOf(area) & CELL_STACKED) == 0)
        {
            return Surface();
        }

        size_t first = 0;
        size_t last = 0;
        StackedRange(inTile, first, last);

        const size_t index = first + size_t(layer) - 1;
        if (index >= last)
        {
            return Surface();
        }

        const StackedLayer& l = m_stacked[index];

        Surface s;
        s.z = RestoreZ(l.z, m_baseZ);
        s.region = l.region;
        s.area = l.area;
        s.clearance = l.clearance;
        s.layer = layer;
        return s;
    }

    // Preferring the surface AT OR BELOW the body, and only falling back to one above
    // it, is what keeps a unit inside a building on the floor it is standing on rather
    // than on the ceiling of the storey beneath. The two are within a couple of yards
    // of each other and a nearest-by-distance rule picks between them by rounding.
    Surface NavTile::SurfaceUnder(int inTile, float z, float tolerance) const
    {
        const uint8_t area = m_area[size_t(inTile)];
        if (!Nav::Walkable(area))
        {
            return Surface();
        }

        Surface best;
        float bestDrop = tolerance;    // distance DOWN from the body to the surface
        Surface bestAbove;
        float bestRise = tolerance;

        std::vector<Surface> surfaces;
        SurfacesAt(inTile, surfaces);

        for (const Surface& s : surfaces)
        {
            const float delta = z - s.z;
            if (delta >= 0.0f)
            {
                if (delta <= bestDrop)
                {
                    bestDrop = delta;
                    best = s;
                }
            }
            else if (-delta <= bestRise)
            {
                bestRise = -delta;
                bestAbove = s;
            }
        }

        if (best.Valid())
        {
            return best;
        }
        return bestAbove;
    }

    size_t NavTile::Footprint() const
    {
        return m_z.size() * sizeof(uint16_t) +
               m_area.size() * sizeof(uint8_t) +
               m_clearance.size() * sizeof(uint8_t) +
               m_region.size() * sizeof(uint16_t) +
               m_stacked.size() * sizeof(StackedLayer) +
               m_regions.size() * sizeof(Region) +
               m_gateways.size() * sizeof(Gateway) +
               m_gatewayCost.size() * sizeof(float) +
               m_links.size() * sizeof(Link);
    }
}

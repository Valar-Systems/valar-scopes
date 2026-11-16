# SCRATCH (map-server): writes fbcounts/s4_render2.cpp (s4_render.cpp + 5d's S4-3 SEAM finder, inserted after the BREAK line) and
# fbcounts/s4_build2.sh (s4_build.sh for s4_render2). Every anchor must match exactly once; refuses if either output exists.
#   python -X utf8 -I mirror/mk_render2.py <fbcounts dir>
import os, sys

D = sys.argv[1]
src, dst = os.path.join(D, "s4_render.cpp"), os.path.join(D, "s4_render2.cpp")
bsrc, bdst = os.path.join(D, "s4_build.sh"), os.path.join(D, "s4_build2.sh")
if os.path.exists(dst) or os.path.exists(bdst):
    print("refused: an output exists"); sys.exit(2)
s = open(src, encoding="utf-8", newline="").read()
assert "\r" not in s
BS = chr(92)
anchor = '        std::printf("BREAK %d segments %zu samples %ld breakpx %zu' + BS + 'n", mi, L.size(), samples, brk.size());\n'
assert s.count(anchor) == 1, "BREAK anchor"
add = """        // 5d, S4-3's SEAM PIXEL (seam25.pred S4-3): a LAND pixel within 1 px of L (the sample's own pixel, or 1 px either side along
        // L's normal) while the pixels 2 and 3 px from L on BOTH sides, along the normal, are water or river. L is sampled exactly as
        // BREAK samples it. The -seam frame is the all-layers frame with L's sampled pixels marked white and each seam pixel magenta.
        auto Land = [&](double X, double Y) {
            const int i = (int)std::floor(X), j = (int)std::floor(Y);
            if (i < 0 || j < 0 || i >= kS || j >= kS || wxlayer::OffDisc(i, j, kS)) return false;
            return wr[(size_t)j * kS + i] == 0 && w.out[(size_t)j * kS + i] == cL;
        };
        std::set<std::pair<int, int>> seam, lpx;
        long ssamples = 0;
        for (const Seg& s : L) {
            double x1, y1, x2, y2;
            px(s.la1, s.lo1, &x1, &y1);
            px(s.la2, s.lo2, &x2, &y2);
            const double dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
            if (!(len > 0)) continue;
            const double nx = -dy / len, ny = dx / len;
            const int steps = (int)std::ceil(len / 0.5);
            for (int k = 0; k <= steps; ++k) {
                const double sx = x1 + dx * k / steps, sy = y1 + dy * k / steps;
                const int si = (int)std::floor(sx), sj = (int)std::floor(sy);
                if (si < 0 || sj < 0 || si >= kS || sj >= kS || wxlayer::OffDisc(si, sj, kS)) continue;
                ++ssamples;
                lpx.insert(std::make_pair(si, sj));
                if (!(W(sx + 2 * nx, sy + 2 * ny) && W(sx + 3 * nx, sy + 3 * ny) && W(sx - 2 * nx, sy - 2 * ny) && W(sx - 3 * nx, sy - 3 * ny))) continue;
                for (int o = -1; o <= 1; ++o) {
                    const double Px = sx + o * nx, Py = sy + o * ny;
                    if (Land(Px, Py)) seam.insert(std::make_pair((int)std::floor(Px), (int)std::floor(Py)));
                }
            }
        }
        std::printf("SEAM %d samples %ld seampx %zu""" + BS + """n", mi, ssamples, seam.size());
        {
            std::vector<uint16_t> mk = a.out;
            for (const auto& p : lpx) mk[(size_t)p.second * kS + p.first] = 0xFFFF;
            for (const auto& p : seam) mk[(size_t)p.second * kS + p.first] = 0xF81F;
            if (!Write565(pre + "-seam.rgb565", mk)) { std::printf("RIG write""" + BS + """n"); return 2; }
        }
"""
s2 = s.replace(anchor, anchor + add)
u_old = 'std::printf("usage: s4_render <pkg.bmpk>'
assert s2.count(u_old) == 1, "usage anchor"
s2 = s2.replace(u_old, 'std::printf("usage: s4_render2 <pkg.bmpk>')
open(dst, "w", encoding="utf-8", newline="").write(s2)
b = open(bsrc, encoding="utf-8", newline="").read()
assert "\r" not in b
b2 = b.replace("s4_render.cpp", "s4_render2.cpp").replace("s4_build.log", "s4_build2.log")
for o, n in (("rm -f s4_render\n", "rm -f s4_render2\n"), ("-o s4_render ", "-o s4_render2 "), ("[ -x s4_render ]", "[ -x s4_render2 ]"),
             ('echo "s4_render $(stat -c %s s4_render)B $(sha256sum s4_render | cut -c1-12)"', 'echo "s4_render2 $(stat -c %s s4_render2)B $(sha256sum s4_render2 | cut -c1-12)"'),
             ('echo "s4_render MISSING"', 'echo "s4_render2 MISSING"')):
    assert b2.count(o) == 1, o
    b2 = b2.replace(o, n)
open(bdst, "w", encoding="utf-8", newline="").write(b2)
print("written: s4_render2.cpp, s4_build2.sh")

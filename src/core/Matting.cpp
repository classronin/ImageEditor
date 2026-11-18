#include "core/Matting.h"

#include <Eigen/Cholesky>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/Sparse>

#include <algorithm>
#include <cmath>
#include <deque>

namespace ie {

namespace {

void erodeBinary(const std::vector<uint8_t>& src, int w, int h, int r, std::vector<uint8_t>& out)
{
    out.assign((size_t)w * (size_t)h, 0);
    if (r <= 0) {
        out = src;
        return;
    }
    std::vector<uint8_t> tmp((size_t)w * (size_t)h, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = src.data() + (size_t)y * (size_t)w;
        uint8_t* trow = tmp.data() + (size_t)y * (size_t)w;
        for (int x = 0; x < w; ++x) {
            uint8_t v = 1;
            for (int k = -r; k <= r; ++k) {
                const int xx = x + k;
                const int cx = xx < 0 ? 0 : (xx >= w ? w - 1 : xx);
                if (row[cx] == 0) { v = 0; break; }
            }
            trow[x] = v;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t v = 1;
            for (int k = -r; k <= r; ++k) {
                const int yy = y + k;
                const int cy = yy < 0 ? 0 : (yy >= h ? h - 1 : yy);
                if (tmp[(size_t)cy * (size_t)w + (size_t)x] == 0) { v = 0; break; }
            }
            out[(size_t)y * (size_t)w + (size_t)x] = v;
        }
    }
}

}

std::vector<uint8_t> maskToTrimap(const std::vector<uint8_t>& mask, int w, int h, int band)
{
    std::vector<uint8_t> fgRaw((size_t)w * (size_t)h, 0), bgRaw((size_t)w * (size_t)h, 0);
    for (int i = 0; i < w * h; ++i) {
        const bool fg = mask[(size_t)i] >= 128;
        fgRaw[(size_t)i] = fg ? 1 : 0;
        bgRaw[(size_t)i] = fg ? 0 : 1;
    }
    std::vector<uint8_t> fgEr, bgEr;
    erodeBinary(fgRaw, w, h, std::max(1, band), fgEr);
    erodeBinary(bgRaw, w, h, std::max(1, band), bgEr);

    std::vector<uint8_t> tri((size_t)w * (size_t)h, 128);
    for (int i = 0; i < w * h; ++i) {
        if (fgEr[(size_t)i]) tri[(size_t)i] = 255;
        else if (bgEr[(size_t)i]) tri[(size_t)i] = 0;
    }
    return tri;
}

bool closedFormAlpha(const Image& img, const uint8_t* trimap, std::vector<float>& out, std::string* err)
{
    if (img.empty() || !trimap) {
        if (err) *err = "matting: 输入为空";
        return false;
    }
    const int w = img.width(), h = img.height();
    if (w < 3 || h < 3) {
        if (err) *err = "matting: 图像太小";
        return false;
    }

    const uint32_t* px = img.pixels();
    std::vector<float> col((size_t)w * (size_t)h * 3);
    for (size_t i = 0; i < (size_t)w * (size_t)h; ++i) {
        const uint32_t v = px[i];
        col[i * 3 + 0] = (float)(v & 0xff) / 255.0f;
        col[i * 3 + 1] = (float)((v >> 8) & 0xff) / 255.0f;
        col[i * 3 + 2] = (float)((v >> 16) & 0xff) / 255.0f;
    }

    std::vector<int> idx((size_t)w * (size_t)h, -1);
    int unknown = 0;
    for (int i = 0; i < w * h; ++i)
        if (trimap[i] == 128) idx[(size_t)i] = unknown++;

    out.assign((size_t)w * (size_t)h, 0.0f);
    for (int i = 0; i < w * h; ++i) {
        if (trimap[i] == 255) out[(size_t)i] = 1.0f;
        else if (trimap[i] == 0) out[(size_t)i] = 0.0f;
    }
    if (unknown <= 0) return true;
    if (unknown > 400000) {
        if (err) *err = "matting: 未知带过大";
        return false;
    }

    const double eps = 1e-7;
    std::vector<Eigen::Triplet<double>> trip;
    trip.reserve((size_t)unknown * 40);
    Eigen::VectorXd rhs = Eigen::VectorXd::Zero(unknown);

    double win[9][3];
    int winPos[9];
    double L[9][9];

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            int n = 0;
            bool anyUnknown = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int p = (y + dy) * w + (x + dx);
                    winPos[n] = p;
                    const size_t o = (size_t)p * 3;
                    win[n][0] = col[o];
                    win[n][1] = col[o + 1];
                    win[n][2] = col[o + 2];
                    if (idx[(size_t)p] >= 0) anyUnknown = true;
                    ++n;
                }
            }
            if (!anyUnknown) continue;

            double mu[3] = {0, 0, 0};
            for (int i = 0; i < 9; ++i)
                for (int c = 0; c < 3; ++c) mu[c] += win[i][c] / 9.0;

            Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
            for (int i = 0; i < 9; ++i) {
                const Eigen::Vector3d d(win[i][0] - mu[0], win[i][1] - mu[1], win[i][2] - mu[2]);
                cov += d * d.transpose();
            }
            cov /= 9.0;
            cov.diagonal().array() += eps;
            const Eigen::Matrix3d inv = cov.inverse();
            if (!inv.allFinite()) continue;

            for (int i = 0; i < 9; ++i) {
                for (int j = 0; j < 9; ++j) {
                    const Eigen::Vector3d d(win[i][0] - mu[0], win[i][1] - mu[1], win[i][2] - mu[2]);
                    const Eigen::Vector3d e(win[j][0] - mu[0], win[j][1] - mu[1], win[j][2] - mu[2]);
                    const double dot = d.dot(inv * e);
                    L[i][j] = (i == j ? 1.0 : 0.0) - (1.0 + dot) / 9.0;
                }
            }

            for (int i = 0; i < 9; ++i) {
                const int ri = idx[(size_t)winPos[i]];
                if (ri < 0) continue;
                for (int j = 0; j < 9; ++j) {
                    const int rj = idx[(size_t)winPos[j]];
                    if (rj >= 0) trip.emplace_back(ri, rj, L[i][j]);
                    else rhs[ri] -= L[i][j] * (double)out[(size_t)winPos[j]];
                }
            }
        }
    }

    for (int i = 0; i < unknown; ++i) trip.emplace_back(i, i, 1e-6);

    Eigen::SparseMatrix<double> A(unknown, unknown);
    A.setFromTriplets(trip.begin(), trip.end());
    Eigen::VectorXd sol;
    bool okSolve = false;
    if (unknown <= 60000) {
        Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt;
        ldlt.compute(A);
        if (ldlt.info() == Eigen::Success) {
            sol = ldlt.solve(rhs);
            okSolve = true;
        }
    }
    if (!okSolve) {
        Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper,
                                 Eigen::IncompleteCholesky<double>> cg;
        cg.setMaxIterations(800);
        cg.setTolerance(1e-6);
        cg.compute(A);
        sol = cg.solve(rhs);
        okSolve = (cg.info() == Eigen::Success);
    }
    if (!okSolve) {
        if (err) *err = "matting: 线性求解未收敛";
        return false;
    }
    for (int p = 0; p < w * h; ++p) {
        const int id = idx[(size_t)p];
        if (id >= 0) out[(size_t)p] = (float)std::clamp(sol[id], 0.0, 1.0);
    }
    return true;
}


void decontaminateForeground(Image& img, const std::vector<float>& alpha, const uint8_t* trimap)
{
    if (img.empty() || alpha.empty()) return;
    const int w0 = img.width(), h0 = img.height();
    const uint32_t* px = img.pixels();

    std::vector<float> image((size_t)w0 * (size_t)h0 * 3);
    for (size_t i = 0; i < (size_t)w0 * (size_t)h0; ++i) {
        const uint32_t v = px[i];
        image[i * 3 + 0] = (float)(v & 0xffu) / 255.0f;
        image[i * 3 + 1] = (float)((v >> 8) & 0xffu) / 255.0f;
        image[i * 3 + 2] = (float)((v >> 16) & 0xffu) / 255.0f;
    }

    float fm[3] = {0.0f, 0.0f, 0.0f}, bm[3] = {0.0f, 0.0f, 0.0f};
    size_t fcount = 0, bcount = 0;
    for (size_t i = 0; i < (size_t)w0 * (size_t)h0; ++i) {
        const float a = alpha[i];
        if (a > 0.9f) { for (int c = 0; c < 3; ++c) fm[c] += image[i * 3 + c]; ++fcount; }
        if (a < 0.1f) { for (int c = 0; c < 3; ++c) bm[c] += image[i * 3 + c]; ++bcount; }
    }
    for (int c = 0; c < 3; ++c) {
        fm[c] /= (float)fcount + 1e-5f;
        bm[c] /= (float)bcount + 1e-5f;
    }

    const float regularization = 5e-3f;
    const float gradient_weight = 0.1f;
    const int n_small = 10, n_big = 2, small_size = 32;

    int wp = 1, hp = 1;
    std::vector<float> fp(3), bp(3);
    for (int c = 0; c < 3; ++c) { fp[(size_t)c] = fm[c]; bp[(size_t)c] = bm[c]; }

    const int n_levels = (int)std::ceil(std::log2((double)std::max(w0, h0)));
    std::vector<float> Ffull, Bfull;

    for (int lv = 0; lv <= n_levels; ++lv) {
        const int wl = (int)std::round(std::pow((double)w0, (double)lv / (double)n_levels));
        const int hl = (int)std::round(std::pow((double)h0, (double)lv / (double)n_levels));
        if (wl < 1 || hl < 1) continue;

        std::vector<float> im((size_t)wl * (size_t)hl * 3), al((size_t)wl * (size_t)hl);
        std::vector<float> F((size_t)wl * (size_t)hl * 3), B((size_t)wl * (size_t)hl * 3);
        for (int y = 0; y < hl; ++y) {
            const int ys = std::min(h0 - 1, (int)((int64_t)y * h0 / hl));
            for (int x = 0; x < wl; ++x) {
                const int xs = std::min(w0 - 1, (int)((int64_t)x * w0 / wl));
                const size_t di = (size_t)y * (size_t)wl + (size_t)x;
                const size_t si = (size_t)ys * (size_t)w0 + (size_t)xs;
                for (int c = 0; c < 3; ++c) im[di * 3 + c] = image[si * 3 + c];
                al[di] = alpha[si];
                const int yq = std::min(hp - 1, (int)((int64_t)y * hp / hl));
                const int xq = std::min(wp - 1, (int)((int64_t)x * wp / wl));
                for (int c = 0; c < 3; ++c) {
                    F[di * 3 + c] = fp[(size_t)(yq * wp + xq) * 3 + c];
                    B[di * 3 + c] = bp[(size_t)(yq * wp + xq) * 3 + c];
                }
            }
        }

        const int n_iter = (wl <= small_size && hl <= small_size) ? n_small : n_big;
        const int dx[4] = {-1, 1, 0, 0};
        const int dy[4] = {0, 0, -1, 1};

        for (int it = 0; it < n_iter; ++it) {
            for (int y = 0; y < hl; ++y) {
                for (int x = 0; x < wl; ++x) {
                    const size_t p = (size_t)y * (size_t)wl + (size_t)x;
                    const float a0 = al[p], a1 = 1.0f - a0;
                    float a00 = a0 * a0, a01 = a0 * a1, a11 = a1 * a1;
                    float b0[3], b1[3];
                    for (int c = 0; c < 3; ++c) { b0[c] = a0 * im[p * 3 + c]; b1[c] = a1 * im[p * 3 + c]; }
                    for (int k = 0; k < 4; ++k) {
                        const int x2 = std::clamp(x + dx[k], 0, wl - 1);
                        const int y2 = std::clamp(y + dy[k], 0, hl - 1);
                        const size_t q = (size_t)y2 * (size_t)wl + (size_t)x2;
                        const float da = regularization + gradient_weight * std::fabs(a0 - al[q]);
                        a00 += da; a11 += da;
                        for (int c = 0; c < 3; ++c) {
                            b0[c] += da * F[q * 3 + c];
                            b1[c] += da * B[q * 3 + c];
                        }
                    }
                    const float det = a00 * a11 - a01 * a01;
                    const float inv = 1.0f / (det == 0.0f ? 1e-6f : det);
                    const float b00 = inv * a11, b01 = inv * -a01, b11 = inv * a00;
                    for (int c = 0; c < 3; ++c) {
                        float fv = b00 * b0[c] + b01 * b1[c];
                        float bv = b01 * b0[c] + b11 * b1[c];
                        F[p * 3 + c] = std::clamp(fv, 0.0f, 1.0f);
                        B[p * 3 + c] = std::clamp(bv, 0.0f, 1.0f);
                    }
                }
            }
        }

        fp = F; bp = B; wp = wl; hp = hl;
        if (lv == n_levels) { Ffull = F; Bfull = B; }
    }

    if (Ffull.empty()) return;
    uint32_t* outPx = img.pixels();
    for (size_t i = 0; i < (size_t)w0 * (size_t)h0; ++i) {
        if (alpha[i] >= 0.95f) continue;
        const float f0 = Ffull[i * 3 + 0] * 255.0f;
        const float f1 = Ffull[i * 3 + 1] * 255.0f;
        const float f2 = Ffull[i * 3 + 2] * 255.0f;
        outPx[i] = (outPx[i] & 0xff000000u) | ((uint32_t)(std::clamp(f0, 0.0f, 255.0f) + 0.5f)) |
                   ((uint32_t)(std::clamp(f1, 0.0f, 255.0f) + 0.5f) << 8) |
                   ((uint32_t)(std::clamp(f2, 0.0f, 255.0f) + 0.5f) << 16);
    }
}

}

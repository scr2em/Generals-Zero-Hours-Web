/*
**	Command & Conquer Generals Zero Hour(tm)
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
** WebAssembly port: diagnostics report, frame statistics and the -webd3d8*
** command line options.
*/
#include "diag.h"
#include "glcount.h"

#include <cstdarg>
#include <emscripten.h>

// The C runtime's argument vector, defined by Dependencies/WebCompat when linked
// into the game. Weak so that the library also links without it.
extern "C" {
extern int __argc __attribute__((weak));
extern char **__argv __attribute__((weak));
}

namespace webd3d8 {

bool g_diagOn = false;
GLCounters g_gl = {};
D3DCounters g_d3d = {};
TotalCounters g_totals = {};

namespace {

struct Entry
{
    Hit kind = Hit::Unsupported;
    uint64_t total = 0;
    uint64_t interval = 0;
};

struct Diag
{
    std::unordered_map<std::string, Entry> hits;
    std::unordered_map<const char *, Entry *> literal; ///< fast path for string literal keys
    std::string gpu;
    double intervalMs = 10000.0;
    double lastReportMs = 0;
    uint64_t intervalFrames = 0;
    // Accumulated since the last report.
    uint64_t sumDraws = 0, sumIndexed = 0, sumUP = 0, sumPrims = 0;
    uint64_t sumGl[GLC_COUNT] = {};
    uint64_t sumGlTotal = 0, sumSync = 0, sumBytes = 0;
    uint64_t sumRS = 0, sumTSS = 0, sumTex = 0, sumXform = 0, sumConst = 0, sumLocks = 0, sumProgSwitch = 0, sumPipe = 0, sumUni = 0;
    uint64_t sumClears = 0, sumRT = 0;
    uint32_t maxDraws = 0, maxGl = 0;
    uint32_t programsCreatedInterval = 0;
    uint64_t sumGroup[16] = {};
    uint64_t sumVao = 0, sumTexBinds = 0;
};

Diag &D()
{
    static Diag d;
    return d;
}

WebD3D8_Stats g_lastFrame;

const char *KindName(Hit k)
{
    switch (k)
    {
    case Hit::Unsupported: return "UNSUPPORTED";
    case Hit::Approximated: return "APPROXIMATED";
    case Hit::Failed: return "FAILED";
    case Hit::Event: return "EVENT";
    case Hit::Perf: return "SLOW-PATH";
    }
    return "?";
}

} // namespace

void DiagHit(Hit kind, const char *key)
{
    Diag &d = D();
    auto lit = d.literal.find(key);
    if (lit != d.literal.end())
    {
        ++lit->second->total;
        ++lit->second->interval;
        return;
    }
    Entry &e = d.hits[key];
    e.kind = kind;
    ++e.total;
    ++e.interval;
    d.literal[key] = &e; // unordered_map nodes are stable; literal keys have static storage
}

void DiagHitf(Hit kind, const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    Entry &e = D().hits[buf];
    e.kind = kind;
    ++e.total;
    ++e.interval;
}

void DiagSetGpu(const std::string &s) { D().gpu = s; }

void DiagPrintReport(const char *reason)
{
    Diag &d = D();
    const double now = emscripten_get_now();
    const double secs = (now - d.lastReportMs) / 1000.0;
    const double frames = d.intervalFrames ? (double)d.intervalFrames : 1.0;
    fprintf(stderr, "[WebD3D8 report] ---- %s: %.1f s, %llu frames (%llu since start) ----\n", reason, secs,
            (unsigned long long)d.intervalFrames, (unsigned long long)g_totals.frames);
    if (!d.gpu.empty()) fprintf(stderr, "[WebD3D8 report] gpu: %s\n", d.gpu.c_str());
    if (d.intervalFrames)
    {
        fprintf(stderr, "[WebD3D8 report] per frame: %.0f draws (%.0f indexed, %.0f user pointer, max %u), %.0f primitives, %.1f clears, %.1f render target switches\n",
                d.sumDraws / frames, d.sumIndexed / frames, d.sumUP / frames, d.maxDraws, d.sumPrims / frames, d.sumClears / frames, d.sumRT / frames);
        fprintf(stderr, "[WebD3D8 report] per frame: %.0f WebGL calls (max %u): draw %.0f, state %.0f, bind %.0f, uniform %.0f, upload %.0f, attrib %.0f, other %.0f, sync %.1f; %.0f KB uploaded\n",
                d.sumGlTotal / frames, d.maxGl, d.sumGl[GLC_DRAW] / frames, d.sumGl[GLC_STATE] / frames, d.sumGl[GLC_BIND] / frames,
                d.sumGl[GLC_UNIFORM] / frames, d.sumGl[GLC_UPLOAD] / frames, d.sumGl[GLC_ATTRIB] / frames, d.sumGl[GLC_OTHER] / frames,
                d.sumSync / frames, d.sumBytes / frames / 1024.0);
        fprintf(stderr, "[WebD3D8 report] per frame: D3D calls: SetRenderState %.0f, SetTextureStageState %.0f, SetTexture %.0f, SetTransform %.0f, shader constants %.0f, locks %.0f; program switches %.0f, pipeline changes %.0f, uniform uploads %.0f; programs compiled in interval %u\n",
                d.sumRS / frames, d.sumTSS / frames, d.sumTex / frames, d.sumXform / frames, d.sumConst / frames, d.sumLocks / frames,
                d.sumProgSwitch / frames, d.sumPipe / frames, d.sumUni / frames, d.programsCreatedInterval);
        fprintf(stderr, "[WebD3D8 report] per frame: uniform groups uploaded: xform %.1f texmat %.1f viewport %.1f clip %.1f lights %.1f material %.1f ambient %.1f fog %.1f tfactor %.1f alpharef %.1f point %.1f lod %.1f bump %.1f border %.1f vsconst %.1f psconst %.1f; texture binds %.1f, new vertex arrays %.2f\n",
                d.sumGroup[0] / frames, d.sumGroup[1] / frames, d.sumGroup[2] / frames, d.sumGroup[3] / frames, d.sumGroup[4] / frames,
                d.sumGroup[5] / frames, d.sumGroup[6] / frames, d.sumGroup[7] / frames, d.sumGroup[8] / frames, d.sumGroup[9] / frames,
                d.sumGroup[10] / frames, d.sumGroup[11] / frames, d.sumGroup[12] / frames, d.sumGroup[13] / frames, d.sumGroup[14] / frames,
                d.sumGroup[15] / frames, d.sumTexBinds / frames, d.sumVao / frames);
    }
    fprintf(stderr, "[WebD3D8 report] context losses %u, restores %u\n", g_totals.contextLosses, g_totals.contextRestores);

    std::vector<std::pair<const std::string *, const Entry *>> rows;
    for (auto &h : d.hits) rows.push_back({&h.first, &h.second});
    std::sort(rows.begin(), rows.end(), [](const std::pair<const std::string *, const Entry *> &a, const std::pair<const std::string *, const Entry *> &b) {
        if (a.second->kind != b.second->kind) return (int)a.second->kind < (int)b.second->kind;
        return a.second->total > b.second->total;
    });
    if (rows.empty())
        fprintf(stderr, "[WebD3D8 report] no unsupported or approximated Direct3D 8 features were hit\n");
    else
    {
        fprintf(stderr, "[WebD3D8 report] feature hits (total since start, +this interval):\n");
        for (auto &r : rows)
            fprintf(stderr, "[WebD3D8 report]   %-12s %-72s x%llu (+%llu)\n", KindName(r.second->kind), r.first->c_str(),
                    (unsigned long long)r.second->total, (unsigned long long)r.second->interval);
    }
    fflush(stderr);

    for (auto &h : d.hits) h.second.interval = 0;
    d.lastReportMs = now;
    d.intervalFrames = 0;
    d.sumDraws = d.sumIndexed = d.sumUP = d.sumPrims = 0;
    memset(d.sumGl, 0, sizeof d.sumGl);
    d.sumGlTotal = d.sumSync = d.sumBytes = 0;
    d.sumRS = d.sumTSS = d.sumTex = d.sumXform = d.sumConst = d.sumLocks = d.sumProgSwitch = d.sumPipe = d.sumUni = 0;
    d.sumClears = d.sumRT = 0;
    d.maxDraws = d.maxGl = 0;
    d.programsCreatedInterval = 0;
    memset(d.sumGroup, 0, sizeof d.sumGroup);
    d.sumVao = d.sumTexBinds = 0;
}

void DiagEndFrame()
{
    Diag &d = D();
    ++g_totals.frames;
    g_totals.draws += g_d3d.draws;
    g_totals.glCalls += g_gl.total;
    g_totals.uploadBytes += g_gl.uploadBytes;

    WebD3D8_Stats &s = g_lastFrame;
    s.frames = (unsigned)g_totals.frames;
    s.draws = g_d3d.draws; s.drawsIndexed = g_d3d.drawsIndexed; s.drawsUserPointer = g_d3d.drawsUP; s.primitives = g_d3d.primitives;
    s.glCalls = g_gl.total; s.glDraw = g_gl.calls[GLC_DRAW]; s.glState = g_gl.calls[GLC_STATE]; s.glBind = g_gl.calls[GLC_BIND];
    s.glUniform = g_gl.calls[GLC_UNIFORM]; s.glUpload = g_gl.calls[GLC_UPLOAD]; s.glAttrib = g_gl.calls[GLC_ATTRIB];
    s.glOther = g_gl.calls[GLC_OTHER]; s.glSync = g_gl.sync; s.uploadBytes = g_gl.uploadBytes;
    s.setRenderState = g_d3d.setRenderState; s.setTextureStageState = g_d3d.setTextureStageState; s.setTexture = g_d3d.setTexture;
    s.setTransform = g_d3d.setTransform; s.setShaderConstant = g_d3d.setShaderConstant; s.locks = g_d3d.locks;
    s.programSwitches = g_d3d.programSwitches; s.pipelineChanges = g_d3d.pipelineChanges; s.uniformUploads = g_d3d.uniformUploads;
    s.programsCreated = g_d3d.programsCreated;
    s.contextLosses = g_totals.contextLosses; s.contextRestores = g_totals.contextRestores;

    if (g_diagOn)
    {
        ++d.intervalFrames;
        d.sumDraws += g_d3d.draws; d.sumIndexed += g_d3d.drawsIndexed; d.sumUP += g_d3d.drawsUP; d.sumPrims += g_d3d.primitives;
        for (int i = 0; i < GLC_COUNT; ++i) d.sumGl[i] += g_gl.calls[i];
        d.sumGlTotal += g_gl.total; d.sumSync += g_gl.sync; d.sumBytes += g_gl.uploadBytes;
        d.sumRS += g_d3d.setRenderState; d.sumTSS += g_d3d.setTextureStageState; d.sumTex += g_d3d.setTexture;
        d.sumXform += g_d3d.setTransform; d.sumConst += g_d3d.setShaderConstant; d.sumLocks += g_d3d.locks;
        d.sumProgSwitch += g_d3d.programSwitches; d.sumPipe += g_d3d.pipelineChanges; d.sumUni += g_d3d.uniformUploads;
        d.sumClears += g_d3d.clears; d.sumRT += g_d3d.rtSwitches;
        d.programsCreatedInterval += g_d3d.programsCreated;
        for (int i = 0; i < 16; ++i) d.sumGroup[i] += g_d3d.groupUploads[i];
        d.sumVao += g_d3d.vaoCreated;
        d.sumTexBinds += g_d3d.textureBinds;
        d.maxDraws = Max(d.maxDraws, g_d3d.draws);
        d.maxGl = Max(d.maxGl, g_gl.total);
        if (emscripten_get_now() - d.lastReportMs >= d.intervalMs) DiagPrintReport("report");
    }
    g_gl = GLCounters{};
    g_d3d = D3DCounters{};
}

void ParseCommandLineOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    if (&__argc == nullptr || &__argv == nullptr || __argv == nullptr) return;
    if (GetConfig().debug) Log("parsing %d command line arguments", __argc);
    WebD3D8_ParseArguments(__argc, __argv);
}

} // namespace webd3d8

extern "C" {

void WebD3D8_ParseArguments(int argc, char **argv)
{
    for (int i = 0; i < argc; ++i)
    {
        const char *a = argv[i];
        if (!a) continue;
        if (strncmp(a, "-webd3d8report", 14) == 0)
            WebD3D8_SetReport(a[14] == '=' ? atoi(a + 15) : 10);
        else if (strcmp(a, "-webd3d8shaders") == 0)
            WebD3D8_SetShaderModel(0x0101, 0x0104);
        else if (strncmp(a, "-webd3d8loseafter=", 18) == 0)
        {
            const char *comma = strchr(a + 18, ',');
            WebD3D8_LoseContextAfterFrames(atoi(a + 18), comma ? atoi(comma + 1) : 120);
        }
    }
}

void WebD3D8_SetReport(int seconds)
{
    using namespace webd3d8;
    D().intervalMs = (seconds > 0 ? seconds : 10) * 1000.0;
    g_diagOn = seconds > 0;
    if (g_diagOn)
    {
        D().lastReportMs = emscripten_get_now();
        fprintf(stderr, "[WebD3D8 report] enabled, every %d s\n", seconds);
    }
}

void WebD3D8_PrintReport(void)
{
    if (webd3d8::g_diagOn) webd3d8::DiagPrintReport("report requested");
}

void WebD3D8_SetFeatureOverrides(int mask) { webd3d8::GetConfig().featureOverrides = mask; }

unsigned WebD3D8_GetHitCount(const char *substring)
{
    using namespace webd3d8;
    unsigned long long n = 0;
    for (auto &h : D().hits)
        if (!substring || h.first.find(substring) != std::string::npos) n += h.second.total;
    return (unsigned)n;
}

void WebD3D8_GetStats(WebD3D8_Stats *out)
{
    if (out) *out = webd3d8::g_lastFrame;
}

} // extern "C"

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
** WebAssembly port: diagnostics. Counts every unsupported or approximated
** Direct3D 8 feature the game actually uses (state values, formats, shader
** opcodes, failed creations, context losses...) and prints a summary every few
** seconds when the report is enabled (-webd3d8report[=seconds] on the command
** line, WebD3D8_SetReport()). Off by default: every hit site is guarded by one
** test of g_diagOn, so nothing is formatted or stored while it is off.
*/
#pragma once

#include "common.h"

namespace webd3d8 {

enum class Hit : uint8_t
{
    Unsupported,  ///< the feature is ignored; the picture may be wrong
    Approximated, ///< emulated with a known visible or numeric difference
    Failed,       ///< a create/compile/lock call failed
    Event,        ///< device level events (context lost/restored...)
    Perf,         ///< a slow path was taken (GPU read-back, CPU decode...)
};

extern bool g_diagOn;

/// Records a hit; `key` is a short stable string literal ("rs WRAP0 ...").
void DiagHit(Hit kind, const char *key);
/// printf style key (only evaluated when the report is on).
void DiagHitf(Hit kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define WD3D_HIT(kind, ...) \
    do { if (::webd3d8::g_diagOn) ::webd3d8::DiagHitf(::webd3d8::Hit::kind, __VA_ARGS__); } while (0)

/// Per-frame Direct3D level counters (reset by Present together with g_gl).
struct D3DCounters
{
    uint32_t draws, drawsIndexed, drawsUP, primitives;
    uint32_t setRenderState, setTextureStageState, setTexture, setTransform, setShaderConstant, setStream, locks;
    uint32_t programSwitches, programsCreated, pipelineChanges, uniformUploads;
    uint32_t clears, rtSwitches, vaoCreated;
    uint32_t groupUploads[16];   ///< per UniformGroup: how often a program received it
    uint32_t textureBinds, stateApplied;
};
extern D3DCounters g_d3d;

/// Cumulative values that survive the frame boundary.
struct TotalCounters
{
    uint64_t frames, draws, glCalls, uploadBytes;
    uint32_t contextLosses, contextRestores;
};
extern TotalCounters g_totals;

/// Called by Present(): rolls the per-frame counters into the totals and the
/// interval, prints the report when its interval has elapsed.
void DiagEndFrame();
/// Prints the report now.
void DiagPrintReport(const char *reason);
/// Sets the GPU description shown in the report header.
void DiagSetGpu(const std::string &s);
/// Parses -webd3d8* options from the C runtime's argument vector (once).
void ParseCommandLineOnce();

} // namespace webd3d8

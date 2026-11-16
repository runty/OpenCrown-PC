// The original wrapper hooks, in installation order. Intentionally included
// with DCP_RENDER_HOOK defined by the table, dispatch and exact AOT filter.
DCP_RENDER_HOOK(0x5650d0, obs_set_vs, 1, 3, 2, false)       // Cue_SetVsShader(cue, VS entry+0x18, 0, fetch, ...)
DCP_RENDER_HOOK(0x565280, obs_set_ps, 1, 3, 2, false)       // Cue_SetPsShader(cue, PS entry+0x18, ...)
DCP_RENDER_HOOK(0x5646d0, obs_set_cb, 1, 3, 2, false)       // Cue_SetConstantBuffer(cue, stage, slot, V#*)
DCP_RENDER_HOOK(0x564030, obs_set_tex, 1, 3, 2, false)      // Cue_SetTexture(cue, stage, slot, T#*)
DCP_RENDER_HOOK(0x5644b0, obs_set_samp, 1, 3, 2, false)     // Cue_SetSampler(cue, stage, slot, S#*)
DCP_RENDER_HOOK(0x564b30, obs_set_vb, 2, 2, 6, true)        // Cue_SetVertexBuffers: mov eax,ecx; test; je
DCP_RENDER_HOOK(0x55ffb0, obs_set_blend, 1, 3, 2, false)    // Dcb_SetBlendControl(dcb, rt, word)
DCP_RENDER_HOOK(0x55fef0, obs_init_default_state, 1, 3, 1, false)  // Dcb_InitDefaultHardwareState(dcb): push rbx
DCP_RENDER_HOOK(0x5606a0, obs_set_prim, 1, 3, 2, false)     // Dcb_SetPrimitiveType(dcb, prim)
DCP_RENDER_HOOK(0x560970, obs_set_index_size, 1, 3, 2, false)
DCP_RENDER_HOOK(0x5612d0, obs_set_scissor, 1, 3, 2, false)  // Dcb_SetScreenScissor(dcb, x0, y0, x1, y1)
DCP_RENDER_HOOK(0x638870, obs_set_rt, 1, 3, 2, false)       // Dcb_SetRenderTarget(dcb, slot, rt*)
DCP_RENDER_HOOK(0x5a7260, obs_exec_sprite, 1, 3, 2, false)
DCP_RENDER_HOOK(0x59f7b0, obs_exec_x1, 1, 3, 2, false)
DCP_RENDER_HOOK(0x5a0330, obs_exec_x2, 1, 3, 2, false)
DCP_RENDER_HOOK(0x5a12e0, obs_exec_x3, 1, 3, 2, false)
DCP_RENDER_HOOK(0x5a15e0, obs_exec_x4, 1, 3, 2, false)
DCP_RENDER_HOOK(0x07ddb0, obs_exec_a, 1, 3, 2, false)
DCP_RENDER_HOOK(0x07eb40, obs_exec_b, 1, 3, 2, false)
DCP_RENDER_HOOK(0x5a3a00, obs_layer_replay, 1, 3, 2, false)

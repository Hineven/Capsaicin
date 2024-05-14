#ifndef MIGI_PROBES_HLSL
#define MIGI_PROBES_HLSL

#include "migi_lib.hlsl"

ProbeHeader GetScreenProbeHeader (int ProbeIndex) {
    ProbeHeaderPacked Packed = g_RWProbeHeaderBuffer[ProbeIndex];
    ProbeHeader Header;
    Header.BasisOffset  = Packed.Packed.x & 0x00FFFFFF;
    Header.Rank         = (Packed.Packed.x >> 24) & 0x0F;
    uint   Flags        = (Packed.Packed.x >> 28) & 0x0F;
    Header.bValid       = (Flags & 0x01) != 0;
    Header.ScreenCoords = UnpackUint16x2(Packed.Packed.y);
    return Header;
}

void WriteScreenProbeHeader (int ProbeIndex, ProbeHeader Header) {
    ProbeHeaderPacked Packed;
    Packed.Packed.x = Header.BasisOffset & 0x00FFFFFF;
    Packed.Packed.x |= (Header.Rank & 0x0F) << 24;
    Packed.Packed.x |= (Header.bValid ? 1 : 0) << 28;
    Packed.Packed.y = PackUint16x2(Header.ScreenCoords);
    g_RWProbeHeaderBuffer[ProbeIndex] = Packed;
}

int2 GetTileJitter (int TileSize) {
    return Hammersley16(MI.FrameSeed % 8, 8, 0) * TileSize;
}

int2 GetScreenProbeScreenPosition (int ProbeIndex) {
    int2 ScreenProbeCoords = int2(ProbeIndex % MI.TileDimensions.x, ProbeIndex / MI.TileDimensions.x);
    int2 TileJitter = GetTileJitter(SSRC_TILE_SIZE);
    int2 UniformScreenProbeScreenPosition = ScreenProbeCoords * SSRC_TILE_SIZE + TileJitter;
    if(ProbeIndex >= MI.UniformScreenProbeCount) {
        ProbeHeader Header = GetScreenProbeHeader(ProbeIndex);
        UniformScreenProbeScreenPosition = Header.ScreenCoords;
    }
    return UniformScreenProbeScreenPosition;
}

int ComputeProbeRankFromSplattedError (int2 ScreenCoords) {
    // TODO: Implement this function
    return 0;
}

int GetProbeBasisCountFromRank (int Rank) {
    return (Rank == 4) ? 12 : (1 << Rank);
}


#endif // MIGI_PROBES_HLSL
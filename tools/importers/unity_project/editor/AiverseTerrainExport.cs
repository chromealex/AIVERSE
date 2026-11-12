// Optional fallback for tools/importers (AIVERSE engine import). The importer decodes TerrainData .asset
// files directly from their serialized type tree; use this script only when that fails (player-build data
// without a type tree, a future serialization change). Copy it into Assets/Editor of YOUR project, run
// Tools > AIVERSE > Export Terrain Data, then import again: the importer reads
// <project>/AiverseExport/terrain/<guid>.json (+ .r16 heights, .splat weights, .details densities) instead.
// Writes only into <project>/AiverseExport; nothing in Assets is changed.
#if UNITY_EDITOR
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

public static class AiverseTerrainExport {
    [MenuItem("Tools/AIVERSE/Export Terrain Data")]
    public static void ExportAll() {
        var dir = Path.Combine(Path.GetDirectoryName(Application.dataPath), "AiverseExport", "terrain");
        Directory.CreateDirectory(dir);
        var guids = AssetDatabase.FindAssets("t:TerrainData");
        foreach (var guid in guids) {
            var data = AssetDatabase.LoadAssetAtPath<TerrainData>(AssetDatabase.GUIDToAssetPath(guid));
            if (data != null) Export(data, guid, dir);
        }
        Debug.Log("AIVERSE: exported " + guids.Length + " TerrainData assets to " + dir);
    }

    static string F(float v) => v.ToString("R", CultureInfo.InvariantCulture);
    static string S(string v) => "\"" + (v ?? "").Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
    static string P(Object o) => o ? S(AssetDatabase.GetAssetPath(o)) : "null";

    static void Export(TerrainData d, string guid, string dir) {
        int res = d.heightmapResolution;
        var h = d.GetHeights(0, 0, res, res);               // [z, x], 0..1
        using (var w = new BinaryWriter(File.Create(Path.Combine(dir, guid + ".r16")))) {
            for (int z = 0; z < res; ++z)
                for (int x = 0; x < res; ++x) w.Write((short)Mathf.RoundToInt(Mathf.Clamp01(h[z, x]) * 32766f));
        }
        int ar = d.alphamapResolution, layers = d.alphamapLayers;
        var a = d.GetAlphamaps(0, 0, ar, ar);                // [z, x, layer]
        using (var w = new BinaryWriter(File.Create(Path.Combine(dir, guid + ".splat")))) {
            for (int l = 0; l < layers; ++l)
                for (int z = 0; z < ar; ++z)
                    for (int x = 0; x < ar; ++x) w.Write((byte)Mathf.RoundToInt(Mathf.Clamp01(a[z, x, l]) * 255f));
        }
        int dr = d.detailResolution, dp = d.detailPrototypes.Length;
        using (var w = new BinaryWriter(File.Create(Path.Combine(dir, guid + ".details")))) {
            for (int l = 0; l < dp; ++l) {
                var g = d.GetDetailLayer(0, 0, dr, dr, l);   // [z, x]
                for (int z = 0; z < dr; ++z)
                    for (int x = 0; x < dr; ++x) w.Write((byte)Mathf.Clamp(g[z, x], 0, 255));
            }
        }
        var sb = new StringBuilder("{\n");
        sb.Append("\"format\": 1, \"source\": ").Append(S(AssetDatabase.GetAssetPath(d))).Append(",\n");
        sb.Append("\"resolution\": ").Append(res).Append(", \"size\": [").Append(F(d.size.x)).Append(", ").Append(F(d.size.y)).Append(", ").Append(F(d.size.z)).Append("],\n");
        sb.Append("\"heights\": ").Append(S(guid + ".r16")).Append(", \"alphamapResolution\": ").Append(ar).Append(", \"alphamaps\": ").Append(S(guid + ".splat")).Append(",\n");
        sb.Append("\"layers\": [");
        var tl = d.terrainLayers;
        for (int i = 0; i < tl.Length; ++i) {
            var l = tl[i];
            if (i > 0) sb.Append(", ");
            if (!l) { sb.Append("{\"name\": \"missing\", \"tile\": [10, 10]}"); continue; }
            sb.Append("{\"name\": ").Append(S(l.name)).Append(", \"source\": ").Append(P(l)).Append(", \"diffuse\": ").Append(P(l.diffuseTexture))
              .Append(", \"normal\": ").Append(P(l.normalMapTexture)).Append(", \"tile\": [").Append(F(l.tileSize.x)).Append(", ").Append(F(l.tileSize.y)).Append("]}");
        }
        sb.Append("],\n\"treePrototypes\": [");
        var tp = d.treePrototypes;
        for (int i = 0; i < tp.Length; ++i) sb.Append(i > 0 ? ", " : "").Append("{\"prefab\": ").Append(P(tp[i].prefab)).Append("}");
        sb.Append("],\n\"trees\": [");
        var ti = d.treeInstances;
        for (int i = 0; i < ti.Length; ++i) {
            var t = ti[i];
            sb.Append(i > 0 ? ",\n" : "").Append("{\"position\": [").Append(F(t.position.x)).Append(", ").Append(F(t.position.y)).Append(", ").Append(F(t.position.z))
              .Append("], \"width\": ").Append(F(t.widthScale)).Append(", \"height\": ").Append(F(t.heightScale)).Append(", \"rotation\": ").Append(F(t.rotation))
              .Append(", \"prototype\": ").Append(t.prototypeIndex).Append("}");
        }
        sb.Append("],\n\"detailResolution\": ").Append(dr).Append(", \"details\": ").Append(S(guid + ".details")).Append(", \"detailPrototypes\": [");
        var dps = d.detailPrototypes;
        for (int i = 0; i < dps.Length; ++i) {
            var p = dps[i];
            sb.Append(i > 0 ? ", " : "").Append("{\"prefab\": ").Append(P(p.prototype)).Append(", \"texture\": ").Append(P(p.prototypeTexture))
              .Append(", \"mesh\": ").Append(p.usePrototypeMesh ? "true" : "false").Append(", \"minWidth\": ").Append(F(p.minWidth)).Append(", \"maxWidth\": ").Append(F(p.maxWidth))
              .Append(", \"minHeight\": ").Append(F(p.minHeight)).Append(", \"maxHeight\": ").Append(F(p.maxHeight)).Append("}");
        }
        sb.Append("]\n}\n");
        File.WriteAllText(Path.Combine(dir, guid + ".json"), sb.ToString());
    }
}
#endif

namespace NightShift;

// Pure selection policy, independently verified without launching the game.
public readonly record struct LodCandidate(int Id, int ParentId, string Name, bool Enabled, bool Active, bool Skinned);

public static class LodPolicy
{
    public static HashSet<int> Select(IEnumerable<LodCandidate> candidates)
    {
        var groups = new Dictionary<(int Parent, string Prefix), List<(int Level, int Id)>>();
        foreach (var c in candidates)
        {
            if (!c.Enabled || !c.Active || !c.Skinned) continue;
            int marker = c.Name.LastIndexOf("_LOD_", StringComparison.OrdinalIgnoreCase);
            if (marker <= 0 || !int.TryParse(c.Name.AsSpan(marker + 5), out int level) || level < 0) continue;
            var key = (c.ParentId, c.Name[..marker].ToUpperInvariant());
            if (!groups.TryGetValue(key, out var values)) groups[key] = values = new();
            values.Add((level, c.Id));
        }
        var result = new HashSet<int>();
        foreach (var values in groups.Values)
        {
            // Ambiguous duplicated names/levels may represent separate body parts.
            // A fully enabled level zero must exist; never pick a disabled winner.
            if (values.Count < 2 || !values.Any(v => v.Level == 0) || values.Select(v => v.Level).Distinct().Count() != values.Count) continue;
            foreach (var v in values) if (v.Level > 0) result.Add(v.Id);
        }
        return result;
    }
}

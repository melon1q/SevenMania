using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using BmsSongSelection.Models;

namespace BmsSongSelection.Services;

public sealed class SongSelectionModel
{
    private readonly List<SongGroup> _groups = new();
    private readonly Dictionary<string, SongGroup> _groupsByDirectory = new(StringComparer.OrdinalIgnoreCase);
    private string? _selectedDirectory;

    public IReadOnlyList<SongGroup> Groups => _groups;
    public bool SortDescending { get; private set; }
    public SongGroup? SelectedGroup => _selectedDirectory is not null &&
        _groupsByDirectory.TryGetValue(_selectedDirectory, out var group) ? group : null;
    public ChartSummary? SelectedChart => SelectedGroup?.SelectedChart;

    public void Clear()
    {
        _groups.Clear();
        _groupsByDirectory.Clear();
        _selectedDirectory = null;
    }

    public void Rebuild(IEnumerable<ChartSummary> charts)
    {
        Clear();
        var orderedCharts = charts.OrderBy(chart => chart, Comparer<ChartSummary>.Create(CompareLibraryCharts)).ToList();
        var baseTitles = CanonicalSongTitles(orderedCharts);
        var grouped = new Dictionary<string, List<(ChartSummary Chart, string Title)>>(StringComparer.OrdinalIgnoreCase);

        for (var index = 0; index < orderedCharts.Count; index++)
        {
            var chart = orderedCharts[index];
            var directory = SongDirectoryIdentity(chart.Path);
            if (!grouped.TryGetValue(directory, out var groupCharts))
            {
                groupCharts = new List<(ChartSummary, string)>();
                grouped.Add(directory, groupCharts);
            }
            groupCharts.Add((chart, baseTitles[index]));
        }

        foreach (var entry in grouped.Values)
        {
            var first = entry[0].Chart;
            var variants = entry.Select(item => item.Chart)
                .OrderBy(chart => chart, Comparer<ChartSummary>.Create(CompareVariants)).ToArray();
            var groupTitle = entry[0].Title;
            var preview = variants.FirstOrDefault(chart => chart.PreviewStartDefined &&
                (chart.DurationSeconds.GetValueOrDefault() <= 0 ||
                    chart.PreviewStartSeconds.GetValueOrDefault() < chart.DurationSeconds.GetValueOrDefault())) ?? variants[0];
            var group = new SongGroup(groupTitle, first.Artist, SongDirectoryIdentity(first.Path),
                first.ModeOrder, first.ModeLabel, variants, preview.Id);
            _groups.Add(group);
        }

        Sort();
    }

    public void Sort()
    {
        var ordered = _groups.OrderBy(group => group, Comparer<SongGroup>.Create(CompareGroups)).ToArray();
        _groups.Clear();
        _groups.AddRange(ordered);
        RebuildDirectoryIndex();
    }

    public void ToggleSort()
    {
        SortDescending = !SortDescending;
        Sort();
    }

    public bool SelectGroup(SongGroup? group)
    {
        if (group is null || !_groupsByDirectory.ContainsKey(group.Directory))
        {
            _selectedDirectory = null;
            return false;
        }
        _selectedDirectory = group.Directory;
        return true;
    }

    public bool CycleSelectedVariant(int delta) => SelectedGroup?.CycleVariant(delta) ?? false;

    private int CompareGroups(SongGroup left, SongGroup right)
    {
        var leftGroup = SongNameGroup(left.Title);
        var rightGroup = SongNameGroup(right.Title);
        var order = leftGroup.CompareTo(rightGroup);
        if (order == 0) order = CompareTitleText(left.Title, right.Title, leftGroup);
        if (order == 0) order = CompareTitleText(left.Artist, right.Artist, 2);
        if (order == 0) order = left.ModeOrder.CompareTo(right.ModeOrder);
        if (order == 0) order = CompareTitleText(left.Directory, right.Directory, 2);
        return SortDescending ? -order : order;
    }

    private static int CompareLibraryCharts(ChartSummary left, ChartSummary right)
    {
        var order = ComparePlayLevel(left.PlayLevel, right.PlayLevel);
        var leftGroup = SongNameGroup(left.Title);
        var rightGroup = SongNameGroup(right.Title);
        if (order == 0) order = leftGroup.CompareTo(rightGroup);
        if (order == 0) order = CompareTitleText(left.Title, right.Title, leftGroup);
        if (order == 0) order = CompareTitleText(left.Path, right.Path, 2);
        return order;
    }

    private static int CompareVariants(ChartSummary left, ChartSummary right)
    {
        var order = right.Playable.CompareTo(left.Playable);
        if (order == 0) order = ComparePlayLevel(left.PlayLevel, right.PlayLevel);
        var subtitleGroup = SongNameGroup(left.Subtitle);
        if (order == 0) order = CompareTitleText(left.Subtitle, right.Subtitle, subtitleGroup);
        if (order == 0) order = CompareTitleText(left.Path, right.Path, 2);
        return order;
    }

    private static int ComparePlayLevel(string left, string right)
    {
        var hasLeft = TryNumericLevel(left, out var levelLeft);
        var hasRight = TryNumericLevel(right, out var levelRight);
        if (hasLeft && hasRight) return levelLeft.CompareTo(levelRight);
        if (hasLeft != hasRight) return hasLeft ? -1 : 1;
        return 0;
    }

    private static bool TryNumericLevel(string text, out double level) =>
        double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out level) &&
        !double.IsNaN(level) && !double.IsInfinity(level);

    private static int SongNameGroup(string value)
    {
        foreach (var character in value)
        {
            if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')) return 0;
            if ((character >= '\uAC00' && character <= '\uD7A3') || (character >= '\u1100' && character <= '\u11FF') ||
                (character >= '\u3130' && character <= '\u318F') || (character >= '\uA960' && character <= '\uA97F') ||
                (character >= '\uD7B0' && character <= '\uD7FF')) return 1;
            if (!char.IsWhiteSpace(character) && !char.IsPunctuation(character) && !(character >= '0' && character <= '9')) return 2;
        }
        return 2;
    }

    private static int CompareTitleText(string left, string right, int group)
    {
        if (group == 0) return StringComparer.OrdinalIgnoreCase.Compare(left, right);
        var culture = group == 1 ? CultureInfo.GetCultureInfo("ko-KR") : CultureInfo.GetCultureInfo("ja-JP");
        var result = culture.CompareInfo.Compare(left, right, CompareOptions.IgnoreCase);
        return result != 0 ? result : StringComparer.OrdinalIgnoreCase.Compare(left, right);
    }

    private static string[] CanonicalSongTitles(IReadOnlyList<ChartSummary> charts)
    {
        var titles = charts.Select(chart => SongTitleWithoutChartLabel(chart.Title)).ToArray();
        var siblings = new Dictionary<string, List<int>>(StringComparer.OrdinalIgnoreCase);
        for (var index = 0; index < charts.Count; index++)
        {
            var identity = SongCollectionIdentity(charts[index].Path);
            if (!siblings.TryGetValue(identity, out var indices))
            {
                indices = new List<int>();
                siblings.Add(identity, indices);
            }
            indices.Add(index);
        }

        var candidates = (string[])titles.Clone();
        foreach (var indices in siblings.Values)
        {
            foreach (var index in indices)
            {
                var full = candidates[index].ToLowerInvariant();
                foreach (var siblingIndex in indices)
                {
                    var baseTitle = candidates[siblingIndex];
                    if (baseTitle.Length == 0 || baseTitle.Length >= titles[index].Length || !full.StartsWith(baseTitle.ToLowerInvariant(), StringComparison.Ordinal)) continue;
                    if (!char.IsWhiteSpace(full[baseTitle.Length])) continue;
                    var suffix = TrimSongTitle(full.Substring(baseTitle.Length));
                    var roundLabel = suffix.Length > 2 && suffix[0] == '(' && suffix[suffix.Length - 1] == ')';
                    var dashLabel = suffix.Length > 2 && suffix[0] == '-' && char.IsWhiteSpace(suffix[1]);
                    if (roundLabel || dashLabel) titles[index] = baseTitle;
                }
            }
        }
        return titles;
    }

    private static string SongTitleWithoutChartLabel(string title)
    {
        var text = TrimSongTitle(title);
        var stripped = false;
        var characters = text.ToCharArray();
        for (var index = 0; index < characters.Length; index++)
        {
            var character = characters[index];
            if (character >= '\uFF01' && character <= '\uFF5E') character = (char)(character - 0xFEE0);
            if (character == '\u3000') character = ' ';
            if (character == '\uFF0D' || character == '\u2013' || character == '\u2014' || character == '\u2212') character = '-';
            if (character == '\u3010' || character == '\uFF3B') character = '[';
            else if (character == '\u3011' || character == '\uFF3D') character = ']';
            else if (character == '\uFF08') character = '(';
            else if (character == '\uFF09') character = ')';
            characters[index] = character;
        }
        text = new string(characters);

        while (text.Length > 0 && (text[text.Length - 1] == ']' || text[text.Length - 1] == ')'))
        {
            var round = text[text.Length - 1] == ')';
            var closing = round ? ')' : ']';
            var opening = round ? '(' : '[';
            var depth = 0;
            var openingIndex = -1;
            for (var index = text.Length - 1; index >= 0; index--)
            {
                if (text[index] == closing) depth++;
                else if (text[index] == opening && --depth == 0) { openingIndex = index; break; }
            }
            if (openingIndex <= 0) break;
            if (round)
            {
                var label = TrimSongTitle(text.Substring(openingIndex + 1, text.Length - openingIndex - 2)).ToLowerInvariant();
                if (label.StartsWith("sp ", StringComparison.Ordinal) || label.StartsWith("dp ", StringComparison.Ordinal)) label = TrimSongTitle(label.Substring(3)).ToLowerInvariant();
                var difficulty = new[] { "beginner", "basic", "easy", "light", "normal", "hyper", "another", "insane" }
                    .Any(word => label == word || (label.StartsWith(word, StringComparison.Ordinal) && label.Length > word.Length &&
                        (char.IsWhiteSpace(label[word.Length]) || char.IsDigit(label[word.Length]))));
                if (!difficulty) break;
            }
            var candidate = TrimSongTitle(text.Substring(0, openingIndex));
            if (candidate.Length == 0) break;
            text = candidate;
            stripped = true;
        }

        for (var index = 1; index < text.Length; index++)
        {
            var character = text[index];
            if (!char.IsWhiteSpace(character) && character != '-' && character != '_' && character != ':' && character != '~') continue;
            if (!IsDifficultySuffix(text.Substring(index))) continue;
            var candidate = TrimSongTitle(text.Substring(0, index));
            if (candidate.Length > 0) return SongTitleWithoutChartLabel(candidate);
        }
        return stripped ? text : TrimSongTitle(title);
    }

    private static bool IsDifficultySuffix(string label)
    {
        var compact = new string(label.ToLowerInvariant().Where(character =>
            !char.IsWhiteSpace(character) && character != '-' && character != '_' && character != ':' && character != '~').ToArray());
        if (compact.StartsWith("sp", StringComparison.Ordinal) || compact.StartsWith("dp", StringComparison.Ordinal)) compact = compact.Substring(2);
        foreach (var word in new[] { "beginner", "basic", "easy", "light", "normal", "hyper", "another", "insane", "leggendaria", "extra", "expert", "starter" })
        {
            if (!compact.StartsWith(word, StringComparison.Ordinal)) continue;
            var tail = compact.Substring(word.Length);
            if (tail.StartsWith("lv", StringComparison.Ordinal)) tail = tail.Substring(2);
            while (tail.Length > 0 && char.IsDigit(tail[0])) tail = tail.Substring(1);
            return tail.Length == 0 || tail == "key" || tail == "keys";
        }
        return false;
    }

    private static string TrimSongTitle(string title)
    {
        var start = 0;
        var end = title.Length;
        while (start < end && (title[start] == ' ' || title[start] == '\t' || title[start] == '\r' || title[start] == '\n')) start++;
        while (end > start && char.IsWhiteSpace(title[end - 1])) end--;
        return title.Substring(start, end - start);
    }

    private static string SongDirectoryIdentity(string chartPath)
    {
        var full = NormalizePath(chartPath);
        var separator = full.LastIndexOf('/');
        return (separator > 0 ? full.Substring(0, separator) : full).ToLowerInvariant();
    }

    private static string SongCollectionIdentity(string chartPath)
    {
        var directory = SongDirectoryIdentity(chartPath);
        var segments = directory.Split('/');
        for (var index = segments.Length - 1; index >= 0; index--)
        {
            if (!string.Equals(segments[index], "songs", StringComparison.OrdinalIgnoreCase)) continue;
            if (index + 1 >= segments.Length) return directory;
            return string.Join("/", segments.Take(index + 2));
        }
        return directory;
    }

    private static string NormalizePath(string path)
    {
        try { return System.IO.Path.GetFullPath(path).Replace('\\', '/').TrimEnd('/'); }
        catch { return path.Replace('\\', '/').TrimEnd('/'); }
    }

    private void RebuildDirectoryIndex()
    {
        _groupsByDirectory.Clear();
        foreach (var group in _groups)
        {
            if (!_groupsByDirectory.ContainsKey(group.Directory)) _groupsByDirectory.Add(group.Directory, group);
        }
    }
}

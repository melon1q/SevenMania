using System.Collections.Generic;

namespace BmsSongSelection.Models;

public sealed class ChartSummary
{
    public string Id { get; set; } = string.Empty;
    public string Path { get; set; } = string.Empty;
    public string Title { get; set; } = string.Empty;
    public string Subtitle { get; set; } = string.Empty;
    public string Artist { get; set; } = string.Empty;
    public string Genre { get; set; } = string.Empty;
    public string PlayLevel { get; set; } = string.Empty;
    public string Mode { get; set; } = "Unknown";
    public string ModeLabel { get; set; } = "판별 불가";
    public int ModeOrder { get; set; }
    public bool Playable { get; set; }
    public string BlockReason { get; set; } = string.Empty;
    public bool PartialLoad { get; set; }
    public string PartialLoadReason { get; set; } = string.Empty;
    public double? InitialBpm { get; set; }
    public double? DurationSeconds { get; set; }
    public bool PreviewStartDefined { get; set; }
    public double? PreviewStartSeconds { get; set; }
    public double? PreviewDurationSeconds { get; set; } = 20;
    public string BannerPath { get; set; } = string.Empty;
    public string TitleImagePath { get; set; } = string.Empty;
}

public sealed class SongCatalogDocument
{
    public int SchemaVersion { get; set; }
    public string LibraryRoot { get; set; } = string.Empty;
    public ulong ParseSeed { get; set; }
    public List<ChartSummary> Charts { get; set; } = new();
}

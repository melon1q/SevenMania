using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;

namespace BmsSongSelection.Models;

public sealed class SongGroup : INotifyPropertyChanged
{
    private int _selectedVariant;

    public SongGroup(string title, string artist, string directory, int modeOrder,
        string modeLabel, IEnumerable<ChartSummary> charts, string previewChartId)
    {
        Title = title;
        Artist = artist;
        Directory = directory;
        ModeOrder = modeOrder;
        ModeLabel = modeLabel;
        Charts = charts.ToArray();
        PreviewChartId = previewChartId;
    }

    public string Title { get; }
    public string Artist { get; }
    public string Directory { get; }
    public string Id => Directory;
    public int ModeOrder { get; }
    public string ModeLabel { get; }
    public IReadOnlyList<ChartSummary> Charts { get; }
    public string PreviewChartId { get; }
    public bool IsHidden => Charts.Count == 0 || !Charts[0].Playable;

    public int SelectedVariant
    {
        get => _selectedVariant;
        set
        {
            var next = Charts.Count == 0 ? 0 : Math.Min(Math.Max(value, 0), Charts.Count - 1);
            if (_selectedVariant == next) return;
            _selectedVariant = next;
            OnPropertyChanged();
            OnPropertyChanged(nameof(SelectedChart));
        }
    }

    public ChartSummary? SelectedChart
    {
        get => Charts.Count == 0 ? null : Charts[SelectedVariant];
        set
        {
            if (value is null) return;
            for (var index = 0; index < Charts.Count; index++)
            {
                if (ReferenceEquals(Charts[index], value)) { SelectedVariant = index; return; }
            }
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool CycleVariant(int delta)
    {
        if (Charts.Count < 2) return false;
        SelectedVariant = (SelectedVariant + delta % Charts.Count + Charts.Count) % Charts.Count;
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

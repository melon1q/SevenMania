using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Windows.Data;
using System.Windows.Input;
using BmsSongSelection.Models;
using BmsSongSelection.Services;

namespace BmsSongSelection.ViewModels;

public sealed class SongSelectionViewModel : INotifyPropertyChanged
{
    private readonly CppCatalogClient _catalogClient;
    private readonly SongSelectionModel _model = new();
    private CancellationTokenSource? _loadCancellation;
    private SongGroup? _selectedGroup;
    private string _playerExecutable = string.Empty;
    private string _searchText = string.Empty;
    private string _status = "BmsPlayer.exe와 곡 폴더를 선택해 곡 목록을 불러오세요.";
    private string _libraryRoot = string.Empty;
    private bool _showHiddenCharts;
    private bool _isLoading;
    private ulong? _parseSeed;

    public SongSelectionViewModel(CppCatalogClient catalogClient)
    {
        _catalogClient = catalogClient;
        GroupsView = CollectionViewSource.GetDefaultView(Groups);
        GroupsView.Filter = IsVisibleGroup;
        ToggleSortCommand = new RelayCommand(_ => ToggleSort());
        ToggleHiddenCommand = new RelayCommand(_ => ShowHiddenCharts = !ShowHiddenCharts);
        PreviousVariantCommand = new RelayCommand(_ => CycleVariant(-1));
        NextVariantCommand = new RelayCommand(_ => CycleVariant(1));
        CancelLoadCommand = new RelayCommand(_ => CancelLoad(), _ => IsLoading);
    }

    public ObservableCollection<SongGroup> Groups { get; } = new();
    public ICollectionView GroupsView { get; }
    public ICommand ToggleSortCommand { get; }
    public ICommand ToggleHiddenCommand { get; }
    public ICommand PreviousVariantCommand { get; }
    public ICommand NextVariantCommand { get; }
    public ICommand CancelLoadCommand { get; }

    public string PlayerExecutable
    {
        get => _playerExecutable;
        private set { if (SetField(ref _playerExecutable, value)) OnPropertyChanged(nameof(PlayerExecutableLabel)); }
    }

    public string PlayerExecutableLabel => string.IsNullOrWhiteSpace(PlayerExecutable)
        ? "플레이어 미지정" : System.IO.Path.GetFileName(PlayerExecutable);

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (!SetField(ref _searchText, value)) return;
            GroupsView.Refresh();
            OnPropertyChanged(nameof(VisibleGroupCount));
            EnsureSelectionVisible();
        }
    }

    public string Status
    {
        get => _status;
        private set => SetField(ref _status, value);
    }

    public string LibraryRoot
    {
        get => _libraryRoot;
        private set => SetField(ref _libraryRoot, value);
    }

    public bool ShowHiddenCharts
    {
        get => _showHiddenCharts;
        set
        {
            if (!SetField(ref _showHiddenCharts, value)) return;
            GroupsView.Refresh();
            OnPropertyChanged(nameof(HiddenToggleLabel));
            OnPropertyChanged(nameof(VisibleGroupCount));
            EnsureSelectionVisible();
        }
    }

    public string HiddenToggleLabel => ShowHiddenCharts ? "실행 가능 곡 보기" : "차단된 곡 보기";

    public bool IsLoading
    {
        get => _isLoading;
        private set
        {
            if (!SetField(ref _isLoading, value)) return;
            if (CancelLoadCommand is RelayCommand command) command.RaiseCanExecuteChanged();
            OnPropertyChanged(nameof(LoadingVisibility));
        }
    }

    public System.Windows.Visibility LoadingVisibility => IsLoading
        ? System.Windows.Visibility.Visible : System.Windows.Visibility.Collapsed;

    public SongGroup? SelectedGroup
    {
        get => _selectedGroup;
        set
        {
            if (ReferenceEquals(_selectedGroup, value)) return;
            if (_selectedGroup is not null) _selectedGroup.PropertyChanged -= SelectedGroupChanged;
            _selectedGroup = value;
            if (_selectedGroup is not null) _selectedGroup.PropertyChanged += SelectedGroupChanged;
            _model.SelectGroup(value);
            OnPropertyChanged();
            OnPropertyChanged(nameof(SelectedChart));
            OnPropertyChanged(nameof(SelectedStatus));
            OnPropertyChanged(nameof(SelectedBpm));
            OnPropertyChanged(nameof(SelectedDuration));
            OnPropertyChanged(nameof(HasSelectedGroup));
        }
    }

    public ChartSummary? SelectedChart => SelectedGroup?.SelectedChart;
    public bool HasSelectedGroup => SelectedGroup is not null;
    public string SelectedStatus => SelectedChart is null ? "곡을 선택하면 채보 정보가 표시됩니다."
        : !SelectedChart.Playable ? SelectedChart.BlockReason
        : SelectedChart.PartialLoad ? "일부 자료를 불러오지 못했습니다. " + SelectedChart.PartialLoadReason
        : "플레이 가능";
    public string SelectedBpm => SelectedChart?.InitialBpm is double bpm
        ? bpm.ToString("0.##", CultureInfo.InvariantCulture) : "—";
    public string SelectedDuration => SelectedChart?.DurationSeconds is double duration
        ? TimeSpan.FromSeconds(Math.Max(0, duration)).ToString(duration >= 3600 ? @"h\:mm\:ss" : @"m\:ss", CultureInfo.InvariantCulture)
        : "—";
    public int VisibleGroupCount => GroupsView.Cast<object>().Count();
    public int ChartCount => Groups.Sum(group => group.Charts.Count);
    public int HiddenChartCount => Groups.Sum(group => group.Charts.Count(chart => !chart.Playable));
    public string SortLabel => _model.SortDescending ? "Z–A" : "A–Z";
    public string SeedLabel => _parseSeed.HasValue ? _parseSeed.Value.ToString(CultureInfo.InvariantCulture) : "—";
    public bool CanStartPlayback => false;

    public event PropertyChangedEventHandler? PropertyChanged;

    public void SetPlayerExecutable(string path)
    {
        PlayerExecutable = path;
        Status = "플레이어 연결: " + path;
    }

    public async Task LoadFolderAsync(string folder)
    {
        if (IsLoading) return;
        if (string.IsNullOrWhiteSpace(PlayerExecutable) || !File.Exists(PlayerExecutable))
        {
            Status = "먼저 BmsPlayer.exe를 지정하세요.";
            return;
        }
        if (!Directory.Exists(folder))
        {
            Status = "곡 폴더를 찾을 수 없습니다.";
            return;
        }

        var cancellation = new CancellationTokenSource();
        _loadCancellation = cancellation;
        var seed = CreateParseSeed();
        IsLoading = true;
        Status = "곡 폴더를 분석하고 있습니다…";
        try
        {
            var catalog = await _catalogClient.LoadAsync(PlayerExecutable, folder, seed, cancellation.Token);
            _parseSeed = catalog.ParseSeed;
            LibraryRoot = catalog.LibraryRoot;
            _model.Rebuild(catalog.Charts);
            Groups.Clear();
            foreach (var group in _model.Groups) Groups.Add(group);
            GroupsView.Refresh();
            SelectedGroup = GroupsView.Cast<SongGroup>().FirstOrDefault();
            OnPropertyChanged(nameof(ChartCount));
            OnPropertyChanged(nameof(HiddenChartCount));
            OnPropertyChanged(nameof(SeedLabel));
            OnPropertyChanged(nameof(VisibleGroupCount));
            OnPropertyChanged(nameof(SortLabel));
            Status = $"{catalog.Charts.Count:N0}개 채보를 {Groups.Count:N0}개 곡 묶음으로 불러왔습니다.";
        }
        catch (OperationCanceledException)
        {
            Status = "곡 목록 분석을 취소했습니다.";
        }
        catch (Exception error)
        {
            Status = error.Message;
        }
        finally
        {
            if (ReferenceEquals(_loadCancellation, cancellation)) _loadCancellation = null;
            cancellation.Dispose();
            IsLoading = false;
        }
    }

    public void ToggleSort()
    {
        var selected = SelectedGroup;
        _model.ToggleSort();
        for (var index = 0; index < _model.Groups.Count; index++)
        {
            var target = _model.Groups[index];
            var current = Groups.IndexOf(target);
            if (current >= 0 && current != index) Groups.Move(current, index);
        }
        GroupsView.Refresh();
        SelectedGroup = selected;
        OnPropertyChanged(nameof(SortLabel));
    }

    public void CycleVariant(int delta)
    {
        if (!_model.CycleSelectedVariant(delta)) return;
        OnPropertyChanged(nameof(SelectedChart));
        OnPropertyChanged(nameof(SelectedStatus));
        OnPropertyChanged(nameof(SelectedBpm));
        OnPropertyChanged(nameof(SelectedDuration));
    }

    public void StartSelectedChart()
    {
        Status = "선택 곡 직접 재생은 C++ 플레이 경계가 준비되는 다음 단계에서 연결합니다.";
    }

    public void CancelLoad() => _loadCancellation?.Cancel();

    private static ulong CreateParseSeed()
    {
        Span<byte> bytes = stackalloc byte[sizeof(ulong)];
        RandomNumberGenerator.Fill(bytes);
        var seed = BitConverter.ToUInt64(bytes);
        return seed == 0 ? 1UL : seed;
    }

    private bool IsVisibleGroup(object item)
    {
        if (item is not SongGroup group || group.Charts.Count == 0) return false;
        if (group.IsHidden != ShowHiddenCharts) return false;
        if (string.IsNullOrWhiteSpace(SearchText)) return true;
        return Contains(group.Title, SearchText) || Contains(group.Artist, SearchText) ||
            Contains(group.ModeLabel, SearchText) || group.Charts.Any(chart =>
                Contains(chart.Title, SearchText) || Contains(chart.Subtitle, SearchText) ||
                Contains(chart.PlayLevel, SearchText) || Contains(chart.Genre, SearchText));
    }

    private static bool Contains(string value, string query) =>
        value.IndexOf(query, StringComparison.CurrentCultureIgnoreCase) >= 0;

    private void EnsureSelectionVisible()
    {
        if (SelectedGroup is not null && GroupsView.Cast<SongGroup>().Contains(SelectedGroup)) return;
        SelectedGroup = GroupsView.Cast<SongGroup>().FirstOrDefault();
    }

    private void SelectedGroupChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName != nameof(SongGroup.SelectedChart)) return;
        OnPropertyChanged(nameof(SelectedChart));
        OnPropertyChanged(nameof(SelectedStatus));
        OnPropertyChanged(nameof(SelectedBpm));
        OnPropertyChanged(nameof(SelectedDuration));
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

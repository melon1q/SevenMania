using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using BmsSongSelection.Services;
using BmsSongSelection.ViewModels;
using Microsoft.Win32;

namespace BmsSongSelection;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        ViewModel = new SongSelectionViewModel(new CppCatalogClient());
        DataContext = ViewModel;
        TryReadStartupArguments();
    }

    private SongSelectionViewModel ViewModel { get; }

    private void ChoosePlayer_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = "BMS Player 실행 파일 선택",
            Filter = "BMS Player|BmsPlayer.exe|실행 파일|*.exe",
            CheckFileExists = true,
            Multiselect = false
        };
        if (dialog.ShowDialog(this) == true) ViewModel.SetPlayerExecutable(dialog.FileName);
    }

    private async void ChooseFolder_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog
        {
            Title = "BMS 곡 폴더 선택",
            Multiselect = false
        };
        if (dialog.ShowDialog(this) == true) await ViewModel.LoadFolderAsync(dialog.FolderName);
    }

    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (Keyboard.FocusedElement is TextBox) return;
        if (e.Key == Key.S && Keyboard.Modifiers == ModifierKeys.None)
        {
            ViewModel.ToggleSort();
            e.Handled = true;
        }
        else if (e.Key == Key.Left && ViewModel.SelectedGroup is not null)
        {
            ViewModel.CycleVariant(-1);
            e.Handled = true;
        }
        else if (e.Key == Key.Right && ViewModel.SelectedGroup is not null)
        {
            ViewModel.CycleVariant(1);
            e.Handled = true;
        }
        else if (e.Key == Key.Enter && ViewModel.SelectedChart is not null)
        {
            ViewModel.StartSelectedChart();
            e.Handled = true;
        }
    }

    private void TryReadStartupArguments()
    {
        var args = Environment.GetCommandLineArgs().Skip(1).ToArray();
        for (var index = 0; index + 1 < args.Length; index++)
        {
            if (args[index] == "--player") ViewModel.SetPlayerExecutable(args[++index]);
            else if (args[index] == "--library")
            {
                var folder = args[++index];
                Loaded += async (_, _) => await ViewModel.LoadFolderAsync(folder);
            }
        }
        if (string.IsNullOrWhiteSpace(ViewModel.PlayerExecutable))
        {
            var adjacent = System.IO.Path.Combine(AppContext.BaseDirectory, "BmsPlayer.exe");
            if (File.Exists(adjacent)) ViewModel.SetPlayerExecutable(adjacent);
        }
    }
}

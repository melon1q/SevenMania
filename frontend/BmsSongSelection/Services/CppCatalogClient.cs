using System.Diagnostics;
using System.Text;
using System.Text.Json;
using BmsSongSelection.Models;

namespace BmsSongSelection.Services;

public sealed class CppCatalogClient
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public async Task<SongCatalogDocument> LoadAsync(string playerExecutable, string folder, ulong parseSeed,
        CancellationToken cancellationToken)
    {
        if (!File.Exists(playerExecutable)) throw new FileNotFoundException("BMS 플레이어 실행 파일을 찾을 수 없습니다.", playerExecutable);
        if (!Directory.Exists(folder)) throw new DirectoryNotFoundException("곡 폴더를 찾을 수 없습니다: " + folder);

        var start = new ProcessStartInfo
        {
            FileName = playerExecutable,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = new UTF8Encoding(false),
            StandardErrorEncoding = new UTF8Encoding(false)
        };
        start.ArgumentList.Add("--catalog");
        start.ArgumentList.Add(folder);
        start.ArgumentList.Add("--seed");
        start.ArgumentList.Add(parseSeed.ToString(System.Globalization.CultureInfo.InvariantCulture));

        using var process = new Process { StartInfo = start, EnableRaisingEvents = true };
        if (!process.Start()) throw new InvalidOperationException("BMS 카탈로그 프로세스를 시작하지 못했습니다.");

        var standardOutput = process.StandardOutput.ReadToEndAsync();
        var standardError = process.StandardError.ReadToEndAsync();
        using var cancellation = cancellationToken.Register(() =>
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
            catch (System.ComponentModel.Win32Exception) { }
        });

        try
        {
            await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
            await process.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
            throw;
        }

        var output = await standardOutput.ConfigureAwait(false);
        var error = await standardError.ConfigureAwait(false);
        if (process.ExitCode != 0)
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(error)
                ? "카탈로그 생성에 실패했습니다. 종료 코드: " + process.ExitCode
                : error.Trim());

        var catalog = JsonSerializer.Deserialize<SongCatalogDocument>(output, JsonOptions)
            ?? throw new InvalidDataException("C++ 플레이어가 빈 카탈로그를 반환했습니다.");
        if (catalog.SchemaVersion != 1)
            throw new InvalidDataException("지원하지 않는 곡 카탈로그 버전입니다: " + catalog.SchemaVersion);
        if (catalog.ParseSeed != parseSeed)
            throw new InvalidDataException("곡 카탈로그의 RANDOM 시드가 요청과 일치하지 않습니다.");
        return catalog;
    }
}

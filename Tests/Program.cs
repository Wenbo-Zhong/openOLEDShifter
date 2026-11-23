// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
using System.Text.Json;
using OpenOLEDShifter;

await Test("trajectory boundaries and endpoint reversal at every supported radius", () =>
{
    for (int r = 1; r <= 8; r++)
    {
        var p = ShiftPolicy.Points(r); Check(p.Count == (2*r+1)*(2*r+1)); Check(p.Distinct().Count() == p.Count);
        Check(p.All(x => Math.Abs(x.X) <= r && Math.Abs(x.Y) <= r));
        var cycle = new ShiftCycle();
        for (int n = 1; n <= 2*(p.Count-1); n++) Check(cycle.Next(r) == p[ShiftPolicy.PingPong(n,p.Count)]);
        cycle.Reset(); Check(cycle.Next(r) == new PixelOffset(1,0));
    }
    return Task.CompletedTask;
});
await Test("Windows and GNOME state payloads preserve settings", () =>
{
    var legacy = JsonSerializer.Deserialize<DesktopSettings>("""{"IntervalSeconds":200,"Radius":1,"Enabled":true,"Icons":true,"Monitor":"display1"}""")!;
    Check(legacy.Icons && legacy.Monitor == "display1" && legacy.PauseFullscreen);
    var gnome = JsonSerializer.Deserialize<DesktopState>("""{"Settings":{"IntervalSeconds":200,"Radius":1,"Enabled":false,"StartAtLogin":true},"Status":"off"}""")!;
    Check(gnome.Settings.StartAtLogin && !gnome.Settings.Enabled && gnome.Settings.Wallpaper && !gnome.Settings.Icons);
    Check(JsonSerializer.Deserialize<DesktopSettings>(JsonSerializer.Serialize(legacy)) == legacy);
    var dirty = legacy with { Radius = 2 }; Check(legacy.Radius == 1 && dirty.Radius == 2);
    return Task.CompletedTask;
});
await Test("invalid writes are rejected; old values can be normalized", async () =>
{
    var b = new FakeBackend(); var c = new BackendConnection(b);
    try { await c.ApplyAsync(new DesktopSettings(Radius: 0)); throw new Exception("Accepted invalid radius"); }
    catch (ArgumentOutOfRangeException) { }
    Check(b.Applies == 0);
    Check(new DesktopSettings(-1,99).Normalize() is { IntervalSeconds:10, Radius:8 });
});
await Test("refresh and explicit edit are serialized; queued cancellation is honored", async () =>
{
    var b = new FakeBackend(); var c = new BackendConnection(b);
    b.HoldRefresh = true;
    var read = c.RefreshAsync(); await b.Entered.Task;
    var edit = c.ApplyAsync(new DesktopSettings(Radius: 3));
    using var cancellation = new CancellationTokenSource();
    var cancelled = c.PauseAsync(cancellation.Token); cancellation.Cancel();
    try { await cancelled; throw new Exception("Ignored cancellation"); } catch(OperationCanceledException) { }
    Check(b.Applies == 0 && b.Pauses == 0);
    b.Release.SetResult(); await read; Check((await edit).Settings.Radius == 3);
    Check(b.Applies == 1 && b.MaxActive == 1);
});
await Test("failed backend call releases connection for reconnect", async () =>
{
    var b = new FakeBackend { Fail = true }; var c = new BackendConnection(b);
    try { await c.RefreshAsync(); throw new Exception("Expected backend failure"); } catch(IOException) { }
    b.Fail = false; Check((await c.RefreshAsync()).Status == "connected");
});
await Test("pause prevents forced mutation and resume restarts a full interval", () =>
{
    var now = DateTimeOffset.UnixEpoch; var s = new ShiftSchedule(200,now);
    Check(!s.Poll(now.AddSeconds(199), "").Shift);
    Check(s.Poll(now.AddSeconds(200), "").Shift);
    Check(s.Poll(now.AddSeconds(201), "locked", true) is { Restore:true, Shift:false });
    Check(s.Poll(now.AddSeconds(999), "locked", true) is { Restore:false, Shift:false });
    Check(!s.Poll(now.AddSeconds(1000), "").Shift && s.Due == now.AddSeconds(1200));
    Check(s.Poll(now.AddSeconds(1200), "").Shift);
    s.Reset(10,now);Check(s.Poll(now,"",true).Shift && s.Due==now.AddSeconds(10));
    return Task.CompletedTask;
});
await Test("atomic store writes complete snapshots and cleans temporary files", async () =>
{
    string folder = Path.Combine(Path.GetTempPath(), "OpenOLEDShifter-core-"+Guid.NewGuid().ToString("N"));
    string file = Path.Combine(folder,"recovery.json");
    await Task.WhenAll(Enumerable.Range(1,8).Select(r => Task.Run(() => AtomicJsonStore.Write(file,new DesktopSettings(Radius:r)))));
    var value = JsonSerializer.Deserialize<DesktopSettings>(File.ReadAllText(file))!;value.Validate();
    Check(Directory.GetFiles(folder,"*.tmp").Length == 0);
    Check(ShiftPolicy.StillOwned(value, value with { })); Check(!ShiftPolicy.StillOwned(value,value with { Enabled=true }));
    File.Delete(file); Directory.Delete(folder);
});
Console.WriteLine("All 7 cross-platform test groups passed.");

static async Task Test(string name, Func<Task> test) { await test(); Console.WriteLine("PASS "+name); }
static void Check(bool value) { if(!value)throw new InvalidOperationException("Assertion failed"); }

sealed class FakeBackend : IDesktopBackend
{
    public DesktopCapabilities Capabilities { get; } = new("fixture",DesktopFeatures.None,"");
    public bool HoldRefresh, Fail;
    public int Applies, Pauses, MaxActive;
    private int active;
    private DesktopSettings settings = new();
    public TaskCompletionSource Entered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public TaskCompletionSource Release { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public async Task<DesktopState> GetStateAsync(CancellationToken token)
    {
        MaxActive = Math.Max(MaxActive,++active); Entered.TrySetResult();
        try { if(Fail)throw new IOException("fixture offline"); if(HoldRefresh)await Release.Task.WaitAsync(token); return new(settings,"connected"); }
        finally { active--; }
    }
    public Task<DesktopState> ApplyAsync(DesktopSettings value, CancellationToken token)
    { MaxActive=Math.Max(MaxActive,++active);Applies++;settings=value;active--;return Task.FromResult(new DesktopState(settings,"saved")); }
    public Task<DesktopState> PauseAsync(CancellationToken token)
    { Pauses++;return Task.FromResult(new DesktopState(settings with { Enabled=false },"paused")); }
    public Task<DesktopState> ShiftAsync(CancellationToken token) => Task.FromResult(new DesktopState(settings,"shifted"));
}

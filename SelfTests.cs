// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors

using System.Drawing;
using System.Windows.Forms;
using System.Text.Json;

namespace OpenOLEDShifter;

internal static class SelfTests
{
    public static void Run()
    {
        var results = new List<string>();
        string testDir = Path.Combine(Path.GetTempPath(), "OpenOLEDShifter-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(testDir);
        Directory.CreateDirectory("artifacts");
        try
        {
            void Test(string name, Action action) { action(); results.Add("PASS " + name); }
            Test("default orbit matches recovered spiral", () =>
            {
                Point[] expected = [new(0,0),new(1,0),new(1,1),new(0,1),new(-1,1),new(-1,0),new(-1,-1),new(0,-1),new(1,-1)];
                Check(Orbit.Points(1).SequenceEqual(expected));
            });
            Test("spiral covers each physical offset once and stays within radius", () =>
            {
                for (int r = 1; r <= 8; r++)
                {
                    var points = Orbit.Points(r); Check(points.Count == (2*r+1)*(2*r+1));
                    Check(points.Distinct().Count() == points.Count);
                    Check(points.All(p => Math.Abs(p.X) <= r && Math.Abs(p.Y) <= r));
                    for (int i = 1; i < points.Count; i++) Check(Math.Abs(points[i].X-points[i-1].X)+Math.Abs(points[i].Y-points[i-1].Y)==1);
                }
            });
            Test("ping-pong does not accumulate drift and reaches both endpoints", () =>
            {
                Check(Enumerable.Range(0,17).Select(i=>Orbit.PingPong(i,9)).SequenceEqual(new[]{0,1,2,3,4,5,6,7,8,7,6,5,4,3,2,1,0}));
                Check(Orbit.PingPong(int.MaxValue,9) is >= 0 and <= 8);
            });
            Test("wallpaper shift wraps every pixel with no resampling or blank edges", () =>
            {
                using var image = new Bitmap(13,9);
                for (int x=0;x<13;x++) for(int y=0;y<9;y++) image.SetPixel(x,y,Color.FromArgb(255,x*17,y*23,(x+y)*9));
                foreach(var offset in new[]{new Point(1,1),new Point(-1,-1),new Point(0,0),new Point(8,-8)})
                {
                    using var shifted = WallpaperRenderer.Translate(image,offset);
                    for(int x=0;x<13;x++) for(int y=0;y<9;y++)
                        Check(shifted.GetPixel(x,y).ToArgb()==image.GetPixel(((x-offset.X)%13+13)%13,((y-offset.Y)%9+9)%9).ToArgb());
                }
            });
            Test("fit and fill preserve intended background/coverage", () =>
            {
                using var source=new Bitmap(20,10); using(var g=Graphics.FromImage(source))g.Clear(Color.Red);
                using var fit=WallpaperRenderer.Render(source,new Size(40,40),3,Color.Blue,Point.Empty);
                using var fill=WallpaperRenderer.Render(source,new Size(40,40),4,Color.Blue,Point.Empty);
                Check(fit.GetPixel(20,0).ToArgb()==Color.Blue.ToArgb()); Check(fit.GetPixel(20,20).R>245);
                Check(fill.GetPixel(20,0).R>245);
            });
            Test("journal round trip retains ownership and original coordinates", () =>
            {
                var data=new RecoveryState {Windows=[new WindowState {Handle=123,Origin=new(-200,50),Last=new(-199,51),Size=new(300,200)}],Wallpapers=[new(){Monitor="fixture",Original="original.png",Last="ours.png"}]};
                string file=Path.Combine(testDir,"roundtrip.json");Store.Write(file,data);var loaded=RecoveryState.Load(file);
                Check(loaded.Windows[0].Origin==new Point(-200,50));Check(loaded.Windows[0].Size==new Size(300,200));Check(loaded.Wallpapers[0].Original=="original.png");
            });
            using var fixture=new Form { Text="OpenOLEDShifter isolated test fixture",StartPosition=FormStartPosition.Manual,Bounds=new Rectangle(100,100,320,200),ShowInTaskbar=false };
            fixture.Show(); Application.DoEvents();
            var origin=Native.BoundsInParent(fixture.Handle).Location;
            Test("real Win32 move then restore only our test window", () =>
            {
                var engine=new ShiftEngine(Path.Combine(testDir,"win32.json"));engine.ShiftFixture(fixture.Handle,new(1,1));
                Check(Native.BoundsInParent(fixture.Handle).Location==origin+new Size(1,1));Check(engine.Restore());
                Check(Native.BoundsInParent(fixture.Handle).Location==origin);
            });
            Test("process restart recovers an interrupted window shift", () =>
            {
                string journal=Path.Combine(testDir,"restart.json");new ShiftEngine(journal).ShiftFixture(fixture.Handle,new(-1,1));
                Check(new ShiftEngine(journal).Restore()); Check(Native.BoundsInParent(fixture.Handle).Location==origin);
            });
            Test("restoration does not override subsequent user window move", () =>
            {
                var engine=new ShiftEngine(Path.Combine(testDir,"external.json"));engine.ShiftFixture(fixture.Handle,new(1,0));
                var userPosition=origin+new Size(25,25);Native.Move(fixture.Handle,userPosition);
                Check(engine.Restore());Check(Native.BoundsInParent(fixture.Handle).Location==userPosition);Native.Move(fixture.Handle,origin);
            });
            Test("minimized window restores normal position without being unminimized", () =>
            {
                var engine=new ShiftEngine(Path.Combine(testDir,"minimized.json"));engine.ShiftFixture(fixture.Handle,new(1,1));
                fixture.WindowState=FormWindowState.Minimized;Application.DoEvents();
                Check(engine.Restore());Check(fixture.WindowState==FormWindowState.Minimized);
                fixture.WindowState=FormWindowState.Normal;Application.DoEvents();
                Check(Native.BoundsInParent(fixture.Handle).Location==origin);
            });
            Test("wallpaper COM interface reads monitors without modification", () =>
            { using var wallpaper=new WallpaperApi();wallpaper.Api.GetMonitorDevicePathCount(out uint count);Check(count>0);wallpaper.Api.GetMonitorDevicePathAt(0,out string id);wallpaper.Api.GetWallpaper(id,out _);wallpaper.Api.GetPosition(out int mode);Check(mode is >=0 and <=5); });
            File.WriteAllText("artifacts/tests.txt",string.Join('\n',results)+$"\n{results.Count} tests passed.\n");
        }
        catch(Exception e)
        { results.Add("FAIL "+e);File.WriteAllText("artifacts/tests.txt",string.Join('\n',results));Environment.ExitCode=1; }
    }
    private static void Check(bool value) { if(!value)throw new InvalidOperationException("Assertion failed"); }
}

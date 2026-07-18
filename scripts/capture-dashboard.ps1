[CmdletBinding()]
param(
    [string]$OutputPath,
    [long]$WindowHandle = 0
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $PSScriptRoot '..\assets\dashboard.png'
}
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class AgentLatchCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left; public int Top; public int Right; public int Bottom; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rectangle);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string windowName);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@

[AgentLatchCapture]::SetThreadDpiAwarenessContext([IntPtr]::new(-4)) | Out-Null

$window = if ($WindowHandle -ne 0) {
    [IntPtr]::new($WindowHandle)
} else {
    [AgentLatchCapture]::FindWindow('AgentLatch.ControlWindow.v1', $null)
}
if ($window -eq [IntPtr]::Zero) {
    throw 'Open the AgentLatch dashboard before capturing it.'
}
[AgentLatchCapture]::ShowWindow($window, 5) | Out-Null
[AgentLatchCapture]::SetForegroundWindow($window) | Out-Null
Start-Sleep -Milliseconds 300

$rectangle = [AgentLatchCapture+Rect]::new()
if (-not [AgentLatchCapture]::GetWindowRect($window, [ref]$rectangle)) {
    throw 'Could not read the AgentLatch window bounds.'
}

$width = $rectangle.Right - $rectangle.Left
$height = $rectangle.Bottom - $rectangle.Top
$bitmap = [System.Drawing.Bitmap]::new($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($rectangle.Left, $rectangle.Top, 0, 0, [System.Drawing.Size]::new($width, $height))
$fullOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($fullOutputPath)) | Out-Null
$bitmap.Save($fullOutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Write-Host "Captured $fullOutputPath"

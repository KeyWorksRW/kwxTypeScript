#requires -Version 7.0
<#
.SYNOPSIS
    Launch the kwxTypeScript quick/probe app, drive it with window messages, print a bounded verdict.

.DESCRIPTION
    Implements the assertions in ../wxUiEditor/.private/kwxDrive.md, once, for both targets:

        quick   the two form nodes in quick.wxui (no content nodes)
        probe   the full content in quick/tests/quick_probe/probe.wxui

    The script builds (unless -SkipBuild), launches the app detached, finds the frame and the modal
    dialog by title and process id, asserts the target's controls, closes the dialog with
    WM_COMMAND(IDOK), closes the frame with WM_CLOSE, requires exit code 0, and always terminates
    the process tree. It never reads stdin and never waits for keyboard input.

    Output is at most 20 lines and ends with exactly one of:

        VERDICT PASS
        VERDICT FAIL

    Exit code: 0 = PASS, 1 = FAIL.

.PARAMETER Target
    quick (default) or probe.

.PARAMETER TimeoutSeconds
    Deadline for the frame window to appear. Default 60.

.PARAMETER SkipBuild
    Do not run the build script (scripts\verify_lang.cmd already ran it).

.EXAMPLE
    pwsh -NoProfile -ExecutionPolicy Bypass -File tests\drive_quick.ps1 -Target probe
#>

[CmdletBinding()]
param(
    [ValidateSet('quick', 'probe')]
    [string]$Target = 'quick',

    [int]$TimeoutSeconds = 60,

    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$RepoName = Split-Path -Leaf $RepoRoot

# --- repository spec: the only part that differs between the six repositories -------------------
$Spec = @{
    quick = @{
        BuildScript = 'build_quick.cmd'
        Executable  = 'bin/Release/kwxts.exe'
        Bare        = $false
        Arguments   = @('tests/quick/quick_main.ts')
        WorkingDir  = '.'
        Project     = '..\quick\quick.wxui'
    }
    probe = @{
        BuildScript = 'build_probe.cmd'
        Executable  = 'bin/Release/kwxts.exe'
        Bare        = $false
        Arguments   = @('tests/quick_probe/probe_main.ts')
        WorkingDir  = '.'
        Project     = '..\quick\tests\quick_probe\probe.wxui'
    }
}

$Pairs = @(
    '(wxFrame, ctor) (wxFrame, set_title) (wxFrame, set_min_size) (wxFrame, center)',
    '(wxTextCtrl, ctor) (wxTextCtrl, set_value) (wxTextCtrl, create_font)',
    '(wxBoxSizer, ctor) (wxBoxSizer, add_window) (wxBoxSizer, add_sizeritem)',
    '(wxStaticBitmap, ctor) (wxStaticBitmap, bitmap_embed)',
    '(wxStdDialogButtonSizer, ctor) (wxStdDialogButtonSizer, realize)',
    '(wxDialog, ctor) (wxDialog, set_title) (wxDialog, bind_close)'
) -join ' '

$FrameTitle = 'wxFrame test for kwx languages'
$DialogTitle = 'Main Test Dialog'
# --- declared content, read from the form -------------------------------------------------------
# Applicability is a property of the form, not of the target name. `quick` was the content-free
# ratchet, so A2/A4/A5/A6 were skipped by constant -- and once quick.wxui declared controls that
# constant silently skipped the only assertions that could see the missing emitters. The form now
# answers three questions: does the frame own controls, does the dialog carry a text value, and
# does it have a standard-button sizer (and which of its buttons).
function Get-DeclaredContent {
    param([string]$ProjectPath)

    $declared = @{
        Readable       = $false
        FrameControls  = 0
        DialogControls = 0
        Text           = $null
        HasStdButtons  = $false
        HasOk          = $false
        HasCancel      = $false
    }

    $full = Join-Path $RepoRoot $ProjectPath
    if (-not (Test-Path -LiteralPath $full)) { return $declared }

    $document = $null
    try { $document = [xml](Get-Content -LiteralPath $full -Raw) }
    catch { return $declared }

    $forms = @($document.SelectNodes('//node[@class="wxFrame" or @class="wxDialog"]'))
    if ($forms.Count -eq 0) { return $declared }

    # A menu bar, toolbar or status bar is attached to the form rather than handed to a sizer, and
    # a sizer is not a window: none of them can be proven by enumerating child windows.
    $attached = @('wxMenuBar', 'wxStatusBar', 'wxToolBar', 'wxMenu', 'wxMenuItem')

    $declared.Readable = $true
    foreach ($form in $forms) {
        $is_frame = ($form.GetAttribute('class') -eq 'wxFrame')
        foreach ($control in @($form.SelectNodes('.//node'))) {
            $class_name = $control.GetAttribute('class')
            if ([string]::IsNullOrEmpty($class_name)) { continue }

            if ($class_name -eq 'wxStdDialogButtonSizer') {
                $declared.HasStdButtons = $true
                # wxUiEditor writes OK/Cancel only when the button is turned off, so an absent
                # property means the button is present.
                if ($control.GetAttribute('OK') -ne '0') { $declared.HasOk = $true }
                if ($control.GetAttribute('Cancel') -ne '0') { $declared.HasCancel = $true }
                continue
            }
            if ($attached -contains $class_name) { continue }
            if ($class_name.EndsWith('Sizer')) { continue }

            if ($is_frame) {
                $declared.FrameControls += 1
            }
            else {
                $declared.DialogControls += 1
                if ($null -eq $declared.Text -and $control.HasAttribute('value')) {
                    $declared.Text = $control.GetAttribute('value')
                }
            }
        }
    }
    return $declared
}

$WM_GETTEXT = 0x000D
$WM_CLOSE = 0x0010
$WM_COMMAND = 0x0111
$BN_CLICKED = 0
# wxWidgets standard command ids, not the Win32 IDOK/IDCANCEL values: wxID_OK is 5100 and
# wxID_CANCEL is 5101 in every wxWidgets port.
$IDOK = 5100
$IDCANCEL = 5101
$SMTO_ABORTIFHUNG = 0x0002

if (-not ('KvxWin' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class KvxWin
{
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr param);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr param);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hWnd, StringBuilder buffer, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, IntPtr wParam, StringBuilder lParam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern int GetDlgCtrlID(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern bool IsWindowEnabled(IntPtr hWnd);

    public static List<IntPtr> TopLevel()
    {
        List<IntPtr> handles = new List<IntPtr>();
        EnumWindows(delegate (IntPtr h, IntPtr p) { handles.Add(h); return true; }, IntPtr.Zero);
        return handles;
    }

    public static List<IntPtr> Children(IntPtr parent)
    {
        List<IntPtr> handles = new List<IntPtr>();
        if (parent != IntPtr.Zero)
        {
            EnumChildWindows(parent, delegate (IntPtr h, IntPtr p) { handles.Add(h); return true; }, IntPtr.Zero);
        }
        return handles;
    }

    public static int ProcessId(IntPtr hWnd)
    {
        uint pid;
        GetWindowThreadProcessId(hWnd, out pid);
        return unchecked((int)pid);
    }

    public static string ClassName(IntPtr hWnd)
    {
        StringBuilder buffer = new StringBuilder(256);
        GetClassName(hWnd, buffer, buffer.Capacity);
        return buffer.ToString();
    }

    public static string Text(IntPtr hWnd)
    {
        StringBuilder buffer = new StringBuilder(1024);
        IntPtr result;
        SendMessageTimeout(hWnd, 0x000D, (IntPtr)buffer.Capacity, buffer, 0x0002, 2000, out result);
        return buffer.ToString();
    }

    public static bool Post(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        return PostMessage(hWnd, msg, wParam, lParam);
    }

    public static bool Visible(IntPtr hWnd) { return IsWindowVisible(hWnd); }
    public static bool Alive(IntPtr hWnd) { return IsWindow(hWnd); }
    public static bool Enabled(IntPtr hWnd) { return IsWindowEnabled(hWnd); }
    public static int ControlId(IntPtr hWnd) { return GetDlgCtrlID(hWnd); }
}
'@
}

# --- result plumbing ---------------------------------------------------------------------------
$script:Results = [System.Collections.Generic.List[string]]::new()
$script:Passed = $true

function Add-Result {
    param([string]$Id, [string]$Name, [bool]$Condition, [string]$Detail)
    $state = if ($Condition) { 'PASS' } else { 'FAIL' }
    if (-not $Condition) { $script:Passed = $false }
    $script:Results.Add(('{0} {1,-2} {2,-16} {3,-4} {4}' -f $Target, $Id, $Name, $state, $Detail))
}

function Add-Skip {
    param([string]$Id, [string]$Name, [string]$Detail)
    $script:Results.Add(('{0} {1,-2} {2,-16} {3,-4} {4}' -f $Target, $Id, $Name, 'SKIP', $Detail))
}

function Write-Verdict {
    param([string[]]$Leading)
    $lines = @()
    if ($Leading) { $lines += $Leading }
    $lines += $script:Results
    foreach ($line in ($lines | Select-Object -First 20)) { Write-Output $line }
    if ($script:Passed) {
        Write-Output 'VERDICT PASS'
        exit 0
    }
    Write-Output 'VERDICT FAIL'
    exit 1
}

function Get-ProcessTree {
    param([int]$RootId)
    $set = [System.Collections.Generic.List[int]]::new()
    $set.Add($RootId)
    $queue = [System.Collections.Generic.Queue[int]]::new()
    $queue.Enqueue($RootId)
    while ($queue.Count -gt 0) {
        $parent = $queue.Dequeue()
        $children = @(Get-CimInstance -ClassName Win32_Process -Filter "ParentProcessId=$parent" -ErrorAction SilentlyContinue)
        foreach ($child in $children) {
            $childId = [int]$child.ProcessId
            if (-not $set.Contains($childId)) {
                $set.Add($childId)
                $queue.Enqueue($childId)
            }
        }
    }
    return $set.ToArray()
}

function Wait-ForWindow {
    param([int[]]$ProcessIds, [string]$Title, [int]$Seconds)
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        foreach ($handle in [KvxWin]::TopLevel()) {
            if (-not [KvxWin]::Visible($handle)) { continue }
            if ($ProcessIds -notcontains [KvxWin]::ProcessId($handle)) { continue }
            if ([KvxWin]::Text($handle) -eq $Title) { return $handle }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return [IntPtr]::Zero
}

function Stop-Tree {
    param([int]$RootId)
    if ($RootId -le 0) { return }
    & taskkill /T /F /PID $RootId *> $null
}

function Get-WindowFacts {
    param([IntPtr]$Handle)
    return ('hwnd=0x{0:X} pid={1} class={2} title="{3}"' -f [int64]$Handle, [KvxWin]::ProcessId($Handle), [KvxWin]::ClassName($Handle), [KvxWin]::Text($Handle))
}

# --- build gate --------------------------------------------------------------------------------
$cfg = $Spec[$Target]
$Declared = Get-DeclaredContent -ProjectPath $cfg.Project
# A form that cannot be read declares nothing, which would skip every content assertion. That is
# a harness defect, not a pass.
if (-not $Declared.Readable) {
    Add-Result 'D1' 'form-declared' $false "cannot read the target form: $($cfg.Project)"
    Write-Verdict
}
$buildErrors = @()

if (-not $SkipBuild) {
    $buildPath = Join-Path $RepoRoot $cfg.BuildScript
    if (-not (Test-Path -LiteralPath $buildPath)) {
        Add-Result 'B1' 'build-script' $false "missing: $($cfg.BuildScript)"
        Write-Verdict
    }
    Push-Location $RepoRoot
    try {
        $buildOutput = @(& cmd /c $cfg.BuildScript 2>&1 | ForEach-Object { "$_" })
        $buildCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($buildCode -ne 0) {
        $buildErrors = @($buildOutput | Where-Object { $_ -match 'error|:\d+:|expected|undefined|traceback|failed' } | Select-Object -First 16)
        if ($buildErrors.Count -eq 0) { $buildErrors = @($buildOutput | Select-Object -Last 10) }
        $script:Passed = $false
        $script:Results.Add(('{0} B2 {1,-16} {2,-4} exit={3}' -f $Target, 'build', 'FAIL', $buildCode))
        Write-Verdict -Leading $buildErrors
    }
    $script:Results.Add(('{0} B2 {1,-16} {2,-4} exit=0' -f $Target, 'build', 'PASS'))
}

# --- launch ------------------------------------------------------------------------------------
if ([string]::IsNullOrEmpty($cfg.Executable)) {
    Add-Result 'L1' 'launcher' $false "no runnable artifact for target $Target (build is compile-only)"
    Write-Verdict
}

if ($cfg.Bare) {
    $exePath = $cfg.Executable
    if (-not (Get-Command -Name $exePath -ErrorAction SilentlyContinue)) {
        Add-Result 'L1' 'launcher' $false "command not found on PATH: $($cfg.Executable)"
        Write-Verdict
    }
}
else {
    $exePath = Join-Path $RepoRoot $cfg.Executable
    if (-not (Test-Path -LiteralPath $exePath)) {
        Add-Result 'L1' 'launcher' $false "missing: $($cfg.Executable)"
        Write-Verdict
    }
}

$workDir = Join-Path $RepoRoot $cfg.WorkingDir
$processIds = @()
$frame = [IntPtr]::Zero
$dialog = [IntPtr]::Zero
$launchedId = 0
$process = $null
$stderrPath = ''
$stdoutPath = ''

try {
    $stderrPath = Join-Path ([System.IO.Path]::GetTempPath()) ("kwx_drive_{0}_{1}.err" -f $RepoName, $Target)
    $stdoutPath = Join-Path ([System.IO.Path]::GetTempPath()) ("kwx_drive_{0}_{1}.out" -f $RepoName, $Target)
    Remove-Item -LiteralPath $stderrPath -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
    # Do NOT pass -WindowStyle Hidden: the STARTUPINFO show state overrides the app's first
    # ShowWindow() call, so the frame is created invisible and A1 can never match it.
    $process = Start-Process -FilePath $exePath -ArgumentList $cfg.Arguments -WorkingDirectory $workDir -PassThru -RedirectStandardError $stderrPath -RedirectStandardOutput $stdoutPath
    $launchedId = $process.Id
    Start-Sleep -Milliseconds 500
    $processIds = Get-ProcessTree -RootId $launchedId

    $frame = Wait-ForWindow -ProcessIds $processIds -Title $FrameTitle -Seconds $TimeoutSeconds
    if ([int64]$frame -eq 0) {
        # A console host may spawn the real application slightly later; refresh the pid set once.
        Start-Sleep -Seconds 1
        $processIds = Get-ProcessTree -RootId $launchedId
        $frame = Wait-ForWindow -ProcessIds $processIds -Title $FrameTitle -Seconds 5
    }
    Add-Result 'A1' 'frame-window' ([int64]$frame -ne 0) $(if ([int64]$frame -ne 0) { Get-WindowFacts $frame } else { "no window titled `"$FrameTitle`" in pid set" })

    $dialog = [IntPtr]::Zero
    if ([int64]$frame -ne 0) {
        $dialog = Wait-ForWindow -ProcessIds $processIds -Title $DialogTitle -Seconds 15
    }
    Add-Result 'A3' 'dialog-window' ([int64]$dialog -ne 0) $(if ([int64]$dialog -ne 0) { Get-WindowFacts $dialog } else { "no window titled `"$DialogTitle`"" })

    if ($Declared.FrameControls -gt 0) {
        if ([int64]$frame -ne 0) {
            $frameChildren = @([KvxWin]::Children($frame) | Where-Object { [KvxWin]::Visible($_) })
            $childFacts = @($frameChildren | ForEach-Object { '(class={0} id={1} text="{2}")' -f [KvxWin]::ClassName($_), [KvxWin]::ControlId($_), [KvxWin]::Text($_) })
            Add-Result 'A2' 'frame-textctrl' ($frameChildren.Count -ge 1) $(if ($frameChildren.Count -ge 1) { "declared=$($Declared.FrameControls) children=$($frameChildren.Count) $($childFacts -join ' ')" } else { "declared=$($Declared.FrameControls) children=0" })
        }
        else {
            Add-Result 'A2' 'frame-textctrl' $false 'frame window not found'
        }
    }
    else {
        Add-Skip 'A2' 'frame-textctrl' "form declares no frame controls: $($cfg.Project)"
    }

    $okButton = [IntPtr]::Zero
    if ([int64]$dialog -ne 0) {
        $dialogChildren = @([KvxWin]::Children($dialog) | Where-Object { [KvxWin]::Visible($_) })
        $textFacts = @($dialogChildren | ForEach-Object { '(class={0} id={1} text="{2}")' -f [KvxWin]::ClassName($_), [KvxWin]::ControlId($_), [KvxWin]::Text($_) })

        if ($null -ne $Declared.Text) {
            $matching = @($dialogChildren | Where-Object { [KvxWin]::Text($_) -ceq $Declared.Text })
            Add-Result 'A4' 'dialog-text' ($matching.Count -eq 1) "matches=$($matching.Count) $($textFacts -join ' ')"
        }
        else {
            Add-Skip 'A4' 'dialog-text' "form declares no control with a text value: $($cfg.Project)"
        }

        if ($Declared.HasStdButtons) {
            $okButton = @($dialogChildren | Where-Object { ([KvxWin]::ControlId($_) -eq $IDOK) -and ([KvxWin]::Text($_) -eq 'OK') }) | Select-Object -First 1
            $cancelButton = @($dialogChildren | Where-Object { ([KvxWin]::ControlId($_) -eq $IDCANCEL) -and ([KvxWin]::Text($_) -eq 'Cancel') }) | Select-Object -First 1
            $hasOk = $null -ne $okButton
            $hasCancel = $null -ne $cancelButton
            if ($Declared.HasCancel) {
                Add-Result 'A5' 'std-buttons' ($hasOk -and $hasCancel) "idok=$hasOk idcancel=$hasCancel"
            }
            else {
                Add-Result 'A5' 'std-buttons' $hasOk "idok=$hasOk idcancel=$hasCancel"
            }
        }
        else {
            Add-Skip 'A5' 'std-buttons' "form declares no standard-button sizer: $($cfg.Project)"
        }
    }
    else {
        if ($null -ne $Declared.Text) { Add-Result 'A4' 'dialog-text' $false 'dialog window not found' }
        else { Add-Skip 'A4' 'dialog-text' 'form declares no control with a text value' }
        if ($Declared.HasStdButtons) { Add-Result 'A5' 'std-buttons' $false 'dialog window not found' }
        else { Add-Skip 'A5' 'std-buttons' 'form declares no standard-button sizer' }
    }

    if ($Declared.HasStdButtons -and $Declared.HasOk) {
        if ([int64]$dialog -ne 0) {
            $commandId = [IntPtr]($IDOK -bor ($BN_CLICKED -shl 16))
            $lParam = if ($null -ne $okButton -and [int64]$okButton -ne 0) { $okButton } else { [IntPtr]::Zero }
            [void][KvxWin]::Post($dialog, $WM_COMMAND, $commandId, $lParam)
            # wxDialog::EndModal() hides the dialog and exits the nested loop; it does not
            # destroy the window, so the observable is "no longer visible" plus "the frame is
            # enabled again" -- both only happen when the OK command ended the modal state.
            $deadline = (Get-Date).AddSeconds(10)
            while (([KvxWin]::Visible($dialog)) -and ((Get-Date) -lt $deadline)) { Start-Sleep -Milliseconds 200 }
            $modalEnded = (-not [KvxWin]::Visible($dialog)) -and [KvxWin]::Enabled($frame)
            Add-Result 'A6' 'idok-closes' $modalEnded "dialog-visible=$([KvxWin]::Visible($dialog)) frame-enabled=$([KvxWin]::Enabled($frame))"
        }
        else {
            Add-Result 'A6' 'idok-closes' $false 'dialog window not found'
        }
    }
    else {
        Add-Skip 'A6' 'idok-closes' 'form declares no standard-button sizer with an OK button'
    }

    if ([int64]$frame -ne 0) {
        [void][KvxWin]::Post($frame, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
        if ($process.WaitForExit(15000)) {
            $process.Refresh()
            $exitCode = $process.ExitCode
            Add-Result 'A7' 'wm-close-exits' ($exitCode -eq 0) "exit=$exitCode timeout=False"
        }
        else {
            Add-Result 'A7' 'wm-close-exits' $false 'exit=none timeout=True'
        }
    }
    else {
        Add-Result 'A7' 'wm-close-exits' $false 'frame window not found'
    }
}
catch {
    Add-Result 'X1' 'harness' $false ($_.Exception.Message -replace '\s+', ' ')
}
finally {
    Stop-Tree -RootId $launchedId
}

if (-not $script:Passed) {
    # A failed run is only actionable with the launcher's own first error lines.
    $launcherErrors = @()
    if ($stderrPath -ne '' -and (Test-Path -LiteralPath $stderrPath)) {
        $launcherErrors += @(Get-Content -LiteralPath $stderrPath -ErrorAction SilentlyContinue | Where-Object { $_.Trim() -ne '' } | Select-Object -First 4)
    }
    if ($launcherErrors.Count -eq 0 -and $stdoutPath -ne '' -and (Test-Path -LiteralPath $stdoutPath)) {
        $launcherErrors += @(Get-Content -LiteralPath $stdoutPath -ErrorAction SilentlyContinue | Where-Object { $_.Trim() -ne '' } | Select-Object -First 4)
    }
    foreach ($line in $launcherErrors) {
        $script:Results.Add(('{0} L2 {1,-16} {2,-4} {3}' -f $Target, 'launcher-output', 'INFO', $line.Trim()))
    }
    if ($null -ne $process -and $process.HasExited) {
        $script:Results.Add(('{0} L3 {1,-16} {2,-4} {3}' -f $Target, 'launcher-exit', 'INFO', "exit=$($process.ExitCode)"))
    }
}

if ($Declared.FrameControls -gt 0 -or $Declared.DialogControls -gt 0) {
    $script:Results.Add(('{0} pairs {1}' -f $Target, $Pairs))
}

Write-Verdict

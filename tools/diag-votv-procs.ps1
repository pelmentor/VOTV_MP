$procs = Get-Process VotV* -ErrorAction SilentlyContinue
Write-Output ("VotV process count: " + $procs.Count)
foreach ($p in $procs) {
    $rss = [math]::Round($p.WorkingSet64 / 1MB, 1)
    $priv = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
    Write-Output ("PID=" + $p.Id + " name=" + $p.ProcessName + " RSS_MB=" + $rss + " Private_MB=" + $priv + " threads=" + $p.Threads.Count + " StartTime=" + $p.StartTime + " Path=" + $p.Path)
}
Write-Output "----"
Write-Output "All processes with 'voices' in name:"
Get-Process | Where-Object { $_.ProcessName -match 'voic|votv|crash' } | ForEach-Object {
    $rss = [math]::Round($_.WorkingSet64 / 1MB, 1)
    Write-Output ("PID=" + $_.Id + " name=" + $_.ProcessName + " RSS_MB=" + $rss + " threads=" + $_.Threads.Count)
}

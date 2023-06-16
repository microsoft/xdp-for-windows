<#

.SYNOPSIS
This appends the contents of a local file to an Azure storage blob.

#>
param (
    [Parameter(Mandatory = $true)]
    [string]$ContainerName,

    [Parameter(Mandatory = $true)]
    [string]$BlobName,

    [Parameter(Mandatory = $true)]
    [string]$FileName
)

Set-StrictMode -Version 'Latest'
$ErrorActionPreference = 'Stop'

$StorageAccountName = "xdpperfdata"

Write-Verbose "Connecting to storage account $StorageAccountName"
$StorageContext = New-AzStorageContext -StorageAccountName $StorageAccountName -SasToken $env:SAS_TOKEN

Write-Verbose "Opening $ContainerName/$BlobName storage blob"
$Blob = Get-AzStorageBlob -Context $StorageContext -Container $ContainerName -Blob $BlobName

try {
    $File = [System.IO.File]::OpenRead($FileName)

    Write-Verbose "Uploading $FileName to $($Blob.ICloudBlob.uri.AbsoluteUri)..."
    $Blob.BlobBaseClient.AppendBlock($File) | Out-Null
    Write-Verbose "Finished uploading $FileName to $($Blob.ICloudBlob.uri.AbsoluteUri)"
} finally {
    if ($File) {
        $File.Close()
    }
}

# ============================================================================
# Script PowerShell: Copier le modèle YOLO11 sur la carte SD
# ============================================================================
# Usage:
#   .\copy_yolo_to_sd.ps1 [lettre_lecteur]
#
# Exemples:
#   .\copy_yolo_to_sd.ps1 E:
#   .\copy_yolo_to_sd.ps1 F:
#   .\copy_yolo_to_sd.ps1    (détection automatique)
# ============================================================================

param(
    [string]$SDDrive
)

# Fonction pour afficher les messages avec couleurs
function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] " -ForegroundColor Blue -NoNewline
    Write-Host $Message
}

function Write-Success {
    param([string]$Message)
    Write-Host "[✓] " -ForegroundColor Green -NoNewline
    Write-Host $Message
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[⚠] " -ForegroundColor Yellow -NoNewline
    Write-Host $Message
}

function Write-Error {
    param([string]$Message)
    Write-Host "[✗] " -ForegroundColor Red -NoNewline
    Write-Host $Message
}

# Chemin du modèle source
$ModelSource = "components\yolo11_detect\models\p4\yolo11_detect_s8_v1.espdl"
$ModelName = "yolo11_detect_s8_v1.espdl"

# Vérifier que le modèle source existe
if (-not (Test-Path $ModelSource)) {
    Write-Error "Modèle source introuvable: $ModelSource"
    Write-Info "Assurez-vous d'être dans le répertoire racine du projet."
    exit 1
}

# Afficher les informations du modèle
$ModelSize = (Get-Item $ModelSource).Length
$ModelSizeMB = [math]::Round($ModelSize / 1MB, 2)
Write-Info "Modèle trouvé: $ModelSource ($ModelSizeMB MB)"

# Détecter le chemin de la carte SD
if ($SDDrive) {
    # Chemin fourni en argument
    if ($SDDrive -notmatch ':$') {
        $SDDrive = $SDDrive + ':'
    }
    $SDPath = $SDDrive + '\'
} else {
    # Tenter de détecter automatiquement les lecteurs amovibles
    Write-Info "Détection automatique de la carte SD..."

    $RemovableDrives = Get-WmiObject Win32_LogicalDisk |
                       Where-Object {$_.DriveType -eq 2} |
                       Select-Object -ExpandProperty DeviceID

    if ($RemovableDrives.Count -eq 0) {
        Write-Error "Aucune carte SD détectée automatiquement."
        Write-Host ""
        Write-Info "Usage: .\copy_yolo_to_sd.ps1 <lettre_lecteur>"
        Write-Host ""
        Write-Host "Exemples:"
        Write-Host "  .\copy_yolo_to_sd.ps1 E:"
        Write-Host "  .\copy_yolo_to_sd.ps1 F:"
        exit 1
    }

    Write-Host ""
    Write-Info "Lecteurs amovibles détectés:"

    $Index = 1
    foreach ($Drive in $RemovableDrives) {
        $Volume = Get-Volume -DriveLetter $Drive.TrimEnd(':') -ErrorAction SilentlyContinue
        $VolumeName = if ($Volume) { $Volume.FileSystemLabel } else { "Sans nom" }
        $DriveSize = (Get-PSDrive $Drive.TrimEnd(':')).Free / 1GB
        Write-Host "  $Index. $Drive\ ($VolumeName) - $([math]::Round($DriveSize, 2)) GB disponible"
        $Index++
    }
    Write-Host ""

    # Si un seul lecteur, l'utiliser automatiquement
    if ($RemovableDrives.Count -eq 1) {
        $SDDrive = $RemovableDrives[0]
        Write-Info "Utilisation automatique: $SDDrive\"
        $SDPath = $SDDrive + '\'
    } else {
        # Demander à l'utilisateur de choisir
        $Choice = Read-Host "Numéro du lecteur à utiliser"

        try {
            $ChoiceIndex = [int]$Choice - 1
            if ($ChoiceIndex -lt 0 -or $ChoiceIndex -ge $RemovableDrives.Count) {
                Write-Error "Choix invalide."
                exit 1
            }
            $SDDrive = $RemovableDrives[$ChoiceIndex]
            $SDPath = $SDDrive + '\'
        } catch {
            Write-Error "Choix invalide."
            exit 1
        }
    }
}

# Vérifier que le chemin existe
if (-not (Test-Path $SDPath)) {
    Write-Error "Chemin invalide: $SDPath"
    Write-Info "Assurez-vous que la carte SD est bien insérée."
    exit 1
}

# Afficher les informations
Write-Host ""
Write-Info "Configuration:"
Write-Info "  Source: $ModelSource"
Write-Info "  Destination: $SDPath$ModelName"
Write-Info "  Taille: $ModelSizeMB MB"
Write-Host ""

# Vérifier l'espace disponible sur la SD
$SDDriveInfo = Get-PSDrive $SDDrive.TrimEnd(':')
$AvailableSpaceGB = [math]::Round($SDDriveInfo.Free / 1GB, 2)
Write-Info "Espace disponible sur la carte SD: $AvailableSpaceGB GB"

# Vérifier si le fichier existe déjà
$DestPath = Join-Path $SDPath $ModelName
if (Test-Path $DestPath) {
    Write-Warning "Le fichier existe déjà sur la carte SD."

    # Comparer les hashes
    $HashSource = (Get-FileHash $ModelSource -Algorithm MD5).Hash
    $HashDest = (Get-FileHash $DestPath -Algorithm MD5).Hash

    if ($HashSource -eq $HashDest) {
        Write-Success "Le fichier sur la SD est identique au modèle source."
        Write-Host ""
        Write-Info "Rien à faire! Le modèle est déjà à jour sur la carte SD."
        exit 0
    } else {
        Write-Warning "Le fichier sur la SD est différent du modèle source."
        $Replace = Read-Host "Voulez-vous le remplacer? (y/N)"

        if ($Replace -ne 'y' -and $Replace -ne 'Y') {
            Write-Info "Copie annulée."
            exit 0
        }
    }
}

# Copier le fichier
Write-Info "Copie en cours..."
try {
    Copy-Item -Path $ModelSource -Destination $DestPath -Force
} catch {
    Write-Error "La copie a échoué: $_"
    exit 1
}

# Vérifier que la copie a réussi
if (Test-Path $DestPath) {
    Write-Success "Modèle copié avec succès!"

    # Vérifier le hash
    Write-Info "Vérification de l'intégrité..."
    $HashSource = (Get-FileHash $ModelSource -Algorithm MD5).Hash
    $HashDest = (Get-FileHash $DestPath -Algorithm MD5).Hash

    if ($HashSource -eq $HashDest) {
        Write-Success "Hash vérifié: intégrité OK"
        Write-Success "MD5: $HashSource"
    } else {
        Write-Error "Hashes différents! La copie peut être corrompue."
        Write-Error "Source: $HashSource"
        Write-Error "Destination: $HashDest"
        exit 1
    }

    # Afficher la structure finale
    Write-Host ""
    Write-Success "Structure de la carte SD:"
    Write-Host "$SDPath"
    Write-Host "└── $ModelName ($ModelSizeMB MB)"
    Write-Host ""

    Write-Info "Prochaines étapes:"
    Write-Host "  1. Éjectez la carte SD en toute sécurité (Clic droit > Éjecter)"
    Write-Host "  2. Insérez-la dans votre ESP32-P4"
    Write-Host "  3. Modifiez votre fichier YAML (voir PATCH_YOLO_TO_SD.yaml)"
    Write-Host "  4. Compilez avec: esphome compile votre_config.yaml"
    Write-Host ""
    Write-Success "✅ Configuration SD Card prête!"
} else {
    Write-Error "La copie a échoué."
    exit 1
}

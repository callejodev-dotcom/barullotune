# Asinar os instaladores

Sen sinatura, macOS e Windows avisan ao abrir o instalador (funciona igual, pero asusta).
O CI xa está preparado: abonda con engadir os segredos no repositorio de GitHub
(Settings → Secrets and variables → Actions). Mentres non estean, os pasos de firma
sáltanse e todo segue igual ca agora.

## Mac (Apple Developer ID e notarización)

Fai falta a conta de desenvolvedor de Apple (99 €/ano).

| Segredo | Que é |
|---|---|
| `MACOS_CERT_P12` | Os certificados «Developer ID Application» e «Developer ID Installer» exportados xuntos nun .p12, en base64 (`base64 -i certs.p12 \| pbcopy`) |
| `MACOS_CERT_PASSWORD` | O contrasinal do .p12 |
| `MACOS_DEV_ID` | `Developer ID Application: Nome (EQUIPO)` |
| `MACOS_INSTALLER_ID` | `Developer ID Installer: Nome (EQUIPO)` |
| `APPLE_ID` | O correo da conta de Apple |
| `APPLE_TEAM_ID` | O identificador do equipo (10 caracteres) |
| `APPLE_APP_PASSWORD` | Un contrasinal de aplicación (appleid.apple.com → Inicio de sesión e seguridade) |

Con eles, `packaging/package_mac.sh` asina cada bundle co «hardened runtime», asina o .pkg e
notarízao (`notarytool` + `stapler`).

## Windows (Azure Trusted Signing)

Fai falta unha conta de Azure con Trusted Signing (uns 9 $/mes) e un perfil de certificado
validado.

| Segredo | Que é |
|---|---|
| `AZURE_TENANT_ID` | O directorio (tenant) de Azure |
| `AZURE_CLIENT_ID` | O identificador dunha App Registration co rol «Trusted Signing Certificate Profile Signer» |
| `AZURE_CLIENT_SECRET` | Un segredo desa App Registration |
| `AZURE_SIGNING_ENDPOINT` | O endpoint da rexión (por exemplo `https://weu.codesigning.azure.net/`) |
| `AZURE_SIGNING_ACCOUNT` | O nome da conta de Trusted Signing |
| `AZURE_CERTIFICATE_PROFILE` | O nome do perfil de certificado |

Con eles, o CI asina os binarios (a app, o VST3 e o CLAP) antes de facer o instalador, e
despois o instalador.

## Linux

Non se asina: o `.tar.gz` instálase co `instalar.sh` para o teu usuario.

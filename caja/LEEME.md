# Caja Chapa NH (v7)

Para LILYGO T-Display S3 + LiPo **103040 (10 × 30 × 40 mm, 1200 mAh)** + imanes de disco 6 × 3 mm.
La batería va **en plano debajo de la pantalla**, no detrás: la chapa es más alta pero mucho más fina.
Medidas de la placa tomadas del modelo 3D oficial de LILYGO.

| Pieza | Imprimir | Notas |
|---|---|---|
| `chapa_nh_cuerpo.stl` | Cara de la pantalla sobre la cama, sin soportes | Botones flexibles integrados en el marco |
| `chapa_nh_tapa.stl` | Cara lisa sobre la cama, sin soportes | Tapa corredera con clic y 5 huecos para imanes |
| `chapa_nh_contraplaca.stl` | Cara lisa sobre la cama | Va por dentro de la ropa (5 imanes) |
| `chapa_nh_pie.stl` | Tal cual, sobre la base, sin soportes | Pie de mostrador inclinado 15°, 5 imanes por detrás |

- Exterior: 65,6 × 61,6 × 16,2 mm (antes 22,6 mm de grueso). Mirándola de frente: pantalla arriba, batería abajo, USB a la derecha.
- PLA o PETG, capa 0,2 mm, 3 perímetros, relleno 20 %.
- Imanes: 15 discos de 6 × 3 mm si haces chapa, contraplaca y pie. Para otro tamaño: `IMAN_D=8 IMAN_G=2 python3 caja_chapa_nh.py .`
- Antes de pegar los imanes, comprueba que se atraen (tapa con contraplaca y tapa con pie).
- El hueco de la batería es más largo que la batería: sujétala con cinta de doble cara. El cable pasa por detrás de la placa.
- **Conector:** la placa usa JST de 1,25 mm. Muchas 103040 traen otro mayor: cámbialo y comprueba que el rojo va al «+».
- El botón RST del canto queda hacia la batería: se pulsa con la tapa abierta.
- La tapa entra deslizando desde el extremo de la antena y hace clic al final. Para abrirla, engancha la uña en la ranura.

Generar: `python3 caja_chapa_nh.py .` y `python3 caja_chapa_nh.py . --pie` (Python 3 + numpy).
Vista previa: `python3 preview7.py <t-display-s3-full.stl>`.

La versión anterior (batería 802540 detrás de la placa, 22,6 mm de grueso) está en `v6_bateria_detras/`.

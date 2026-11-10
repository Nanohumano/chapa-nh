# Caja Chapa NH (v1)

Para LILYGO T-Display S3 + LiPo 802540 (800 mAh) + 3 imanes de disco.
Medidas de la placa tomadas del modelo 3D oficial de LILYGO.

| Pieza | Imprimir | Notas |
|---|---|---|
| `chapa_nh_cuerpo.stl` | Cara de la pantalla sobre la cama, sin soportes | Botones flexibles integrados en el marco |
| `chapa_nh_tapa.stl` | Cara lisa sobre la cama, sin soportes | 3 huecos para imanes en el lado USB |
| `chapa_nh_contraplaca.stl` | Cara lisa sobre la cama | Va por dentro de la ropa |

- Exterior: 29,8 × 65,6 × 19,9 mm
- PLA o PETG, capa 0,2 mm, 3 perímetros, relleno 20 %
- Imanes previstos: disco 6 × 3 mm (hueco 6,4 × 3,3). Para otro tamaño:
  `IMAN_D=8 IMAN_G=2 python3 caja_chapa_nh.py .`
- Antes de pegar los imanes, comprueba que la tapa y la contraplaca se atraen.
- La zona de la antena (extremo contrario al USB) queda libre de batería e imanes.

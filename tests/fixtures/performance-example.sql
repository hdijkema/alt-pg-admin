-- LTS/LBS - bron-eerst performanceproef, 2026-11-05
-- PostgreSQL tests. Geen aanpassingen aan de productieviews.
-- De CREATE VIEW-statements gebruiken ook in SQLite en SQL Server gangbare
-- constructies; voor SQL Server moet "package" echter [package] worden.
-- De EXPLAIN (ANALYZE, BUFFERS) en EXCEPT ALL tests hieronder zijn PostgreSQL-specifiek.

-- ================================================================
-- 1. Exacte bron-eerst-tegenhanger van lts_change_numbers
-- ================================================================
CREATE OR REPLACE VIEW lts_change_numbers_src_test AS
SELECT DISTINCT
    o.ea_guid AS classguid,
    o.Object_Type AS classtype,
    ch.kind,
    o.Object_ID AS object_id,
    CASE WHEN ch.kind = 'element' THEN NULL ELSE ch.id END AS feature_id,
    ch.feature_guid,
    ch.feature_stereotype,
    o.Alias AS object_alias,
    o.Name AS object_naam,
    o.Stereotype AS object_stereotype,
    ch.feature_alias AS obj_feature_alias,
    ch.feature_name AS obj_feature_naam,
    pkg.Name AS "package",
    parent_pkg.Name AS parent_of_package,
    grandparent_pkg.Name AS parent_of_parent_of_package,
    ch.nr,
    ch.id,
    ch.volgnr,
    ch.change,
    ch.change AS change_nr,
    ch.status,
    LOWER(TRIM(ch.status)) AS change_kind
FROM (
    SELECT object_id, nr, 0 AS id, 'element' AS kind,
           CAST(NULL AS varchar(255)) AS feature_guid,
           CAST(NULL AS varchar(255)) AS feature_stereotype,
           CAST(NULL AS varchar(255)) AS feature_alias,
           CAST(NULL AS varchar(255)) AS feature_name,
           volgnr, change, status
    FROM lts_object_changes
    UNION ALL
    SELECT object_id, nr, id, 'attribute' AS kind,
           feature_guid, feature_stereotype,
           nr AS feature_alias, name AS feature_name,
           volgnr, change, status
    FROM lts_attr_changes
    UNION ALL
    SELECT object_id, nr, id, 'operation' AS kind,
           feature_guid, feature_stereotype,
           nr AS feature_alias, name AS feature_name,
           volgnr, change, status
    FROM lts_op_changes
) ch
JOIN t_object o ON o.Object_ID = ch.object_id
LEFT JOIN t_package pkg ON pkg.Package_ID = o.Package_ID
LEFT JOIN t_package parent_pkg ON parent_pkg.Package_ID = pkg.Parent_ID
LEFT JOIN t_package grandparent_pkg ON grandparent_pkg.Package_ID = parent_pkg.Parent_ID;

-- ================================================================
-- 2. Zelfde releasejoin als productie, maar met bron-eerst-nummers
-- ================================================================
CREATE OR REPLACE VIEW lts_changes_per_release_src_test AS
SELECT
    cn.classguid, cn.classtype, cn.kind,
    cn.object_id, cn.object_alias, cn.object_naam, cn.object_stereotype,
    cn.feature_id, cn.feature_guid, cn.obj_feature_alias,
    cn.obj_feature_naam, cn.feature_stereotype,
    cn."package", cn.parent_of_package, cn.parent_of_parent_of_package,
    cr.change_oid, cr.change_guid,
    cn.change, cn.volgnr,
    cr.originated_in_release, cr.solved_in_release, rv.release_datum,
    cn.status, cn.change_kind
FROM lts_change_numbers_src_test cn
LEFT JOIN lts_changes_releases cr ON cr.change = cn.change
LEFT JOIN lts_release_volgorde rv
  ON UPPER(rv.release) = UPPER(cr.solved_in_release);

-- ================================================================
-- 3. Vergelijk aantal en inhoud (PostgreSQL)
-- ================================================================
-- Alle regels, inclusief duplicaten. Verwacht 0 in beide richtingen.
SELECT 'numbers: productie MINUS test' AS controle, COUNT(*) AS verschillen
FROM (
    SELECT * FROM lts_change_numbers
    EXCEPT ALL
    SELECT * FROM lts_change_numbers_src_test
) d
UNION ALL
SELECT 'numbers: test MINUS productie', COUNT(*)
FROM (
    SELECT * FROM lts_change_numbers_src_test
    EXCEPT ALL
    SELECT * FROM lts_change_numbers
) d
UNION ALL
SELECT 'release: productie MINUS test', COUNT(*)
FROM (
    SELECT * FROM lts_changes_per_release
    EXCEPT ALL
    SELECT * FROM lts_changes_per_release_src_test
) d
UNION ALL
SELECT 'release: test MINUS productie', COUNT(*)
FROM (
    SELECT * FROM lts_changes_per_release_src_test
    EXCEPT ALL
    SELECT * FROM lts_changes_per_release
) d;

-- ================================================================
-- 4. EXPLAIN: eerst beperkt, dan objectnaamselectie
-- ================================================================
-- Tip: voer iedere EXPLAIN afzonderlijk uit en noteer tijd, buffers,
-- het aantal loops bij de bronviews en Rows Removed by Join Filter.

-- A. Eén object: oorspronkelijke view
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM lts_change_numbers
WHERE object_id = 77287 AND kind = 'attribute';

-- B. Eén object: nieuwe view
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM lts_change_numbers_src_test
WHERE object_id = 77287 AND kind = 'attribute';

-- C. Drie objecten: oorspronkelijke view
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM lts_change_numbers
WHERE object_id IN (77287, 77288, 77289) AND kind = 'attribute';

-- D. Drie objecten: nieuwe view
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM lts_change_numbers_src_test
WHERE object_id IN (77287, 77288, 77289) AND kind = 'attribute';

-- E. Naamselectie: oorspronkelijke view
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM lts_change_numbers
WHERE object_id IN (
    SELECT object_id FROM t_object WHERE name LIKE 'BF Hulp%'
) AND kind = 'attribute';

-- F. Naamselectie: nieuwe view
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM lts_change_numbers_src_test
WHERE object_id IN (
    SELECT object_id FROM t_object WHERE name LIKE 'BF Hulp%'
) AND kind = 'attribute';

-- G. Volledige releasekoppeling: productie
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM lts_changes_per_release
WHERE object_id IN (
    SELECT object_id FROM t_object WHERE name LIKE 'BF Hulp%'
) AND kind = 'attribute';

-- H. Volledige releasekoppeling: bron-eerst
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM lts_changes_per_release_src_test
WHERE object_id IN (
    SELECT object_id FROM t_object WHERE name LIKE 'BF Hulp%'
) AND kind = 'attribute';

-- ================================================================
-- 5. Opruimen na de proef (afzonderlijk uitvoeren)
-- ================================================================
-- DROP VIEW IF EXISTS lts_changes_per_release_src_test;
-- DROP VIEW IF EXISTS lts_change_numbers_src_test;

import json, shutil, struct, tempfile, unittest
from pathlib import Path
from unittest import mock
import numpy as np
import manage


class TriangleTests(unittest.TestCase):
    def test_selected_only_and_indices_stay_valid(self):
        raw=struct.pack('<9H',0,1,2,3,4,5,6,7,8)
        out,n=manage.neutralize_triangles(raw,9,[(3,3)],[(0,3),(6,3)])
        self.assertEqual(struct.unpack('<9H',out),(0,1,2,3,3,3,6,7,8))
        self.assertEqual(n,1)
        self.assertEqual(len(out),len(raw))

    def test_refuse_shared_protected_triangles(self):
        with self.assertRaisesRegex(ValueError,'overlap'):
            manage.neutralize_triangles(bytes(12),6,[(0,3)],[(2,3)])

    def test_reject_outside_index_span(self):
        with self.assertRaisesRegex(ValueError,'invalid selected'):
            manage.neutralize_triangles(bytes(12),6,[(3,6)],[])

    def test_reject_non_triangle_count(self):
        with self.assertRaisesRegex(ValueError,'invalid selected'):
            manage.neutralize_triangles(bytes(12),6,[(0,4)],[])

    def test_separate_ranges_preserve_unselected(self):
        before=np.arange(30,dtype='<u2');raw=before.tobytes()
        after,n=manage.neutralize_triangles(raw,30,[(0,6),(21,9)],[(6,15)])
        np.testing.assert_array_equal(np.frombuffer(after,dtype='<u2')[6:21],before[6:21])
        self.assertEqual(n,5)

    def test_named_character_rule_not_all_iconic(self):
        self.assertTrue(manage.female_material(r'R:\Characters\john\sarahallen_gf.matxml'))
        self.assertTrue(manage.female_material(r'R:\Characters\fla25-02.matxml'))
        self.assertFalse(manage.female_material(r'R:\Characters\iconic.matxml'))
        self.assertFalse(manage.female_material(r'R:\Characters\mwt25-01_gm.matxml'))


class TransactionTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.game=Path(self.temp.name)/'game';self.here=self.game/'mod/asset_filter';self.here.mkdir(parents=True)
        self.patches=[mock.patch.object(manage,'GAME',self.game),mock.patch.object(manage,'HERE',self.here),mock.patch.object(manage,'ensure_closed')]
        for p in self.patches:p.start();self.addCleanup(p.stop)
        files=[];ops=[]
        for i,name in enumerate(['a.sp','b.sp']):
            path=self.game/name;path.write_bytes(b'0123456789')
            backup=self.game/'backups'/name;backup.parent.mkdir(exist_ok=True);shutil.copy2(path,backup)
            payload=self.here/f'{i}.bin';payload.write_bytes(b'XX')
            before=path.read_bytes();after=before[:2]+b'XX'+before[4:]
            prepared=self.here/'prepared'/name;prepared.parent.mkdir(exist_ok=True);prepared.write_bytes(after)
            files.append({'source':name,'before_sha256':manage.sha(before),'after_sha256':manage.sha(after),'backup':str(backup.relative_to(self.game)),'prepared':str(prepared.relative_to(self.here))})
            ops.append({'source':name,'offset':2,'bytes':2,'before_sha256':manage.sha(b'23'),'after_sha256':manage.sha(b'XX'),'payload':payload.name})
        self.plan={'files':files,'operations':ops}

    def test_apply_restore_and_idempotence(self):
        manage.apply_or_restore(self.plan)
        for r in self.plan['files']:self.assertEqual((self.game/r['source']).read_bytes(),b'01XX456789')
        manage.apply_or_restore(self.plan)
        manage.apply_or_restore(self.plan,True)
        for r in self.plan['files']:self.assertEqual((self.game/r['source']).read_bytes(),b'0123456789')
        manage.apply_or_restore(self.plan,True)

    def test_unknown_game_changes_not_overwritten(self):
        (self.game/'a.sp').write_bytes(b'external change')
        with self.assertRaisesRegex(ValueError,'unknown archive hashes'):
            manage.apply_or_restore(self.plan)
        self.assertEqual((self.game/'a.sp').read_bytes(),b'external change')
        self.assertEqual((self.game/'b.sp').read_bytes(),b'0123456789')

    def test_rollback_after_second_commit_failure(self):
        replace=manage.os.replace
        def fail_second(src,dst):
            if str(src).endswith('b.sp.asset-filter-staging'):raise OSError('simulated second commit failure')
            return replace(src,dst)
        with mock.patch.object(manage.os,'replace',side_effect=fail_second):
            with self.assertRaisesRegex(OSError,'simulated'):
                manage.apply_or_restore(self.plan)
        for r in self.plan['files']:self.assertEqual((self.game/r['source']).read_bytes(),b'0123456789')
        self.assertFalse(list(self.game.glob('*.asset-filter-staging')))

    def test_payload_tampering_rejected_before_commit(self):
        (self.here/'1.bin').write_bytes(b'YY')
        with self.assertRaisesRegex(ValueError,'payload mismatch'):
            manage.apply_or_restore(self.plan)
        for r in self.plan['files']:self.assertEqual((self.game/r['source']).read_bytes(),b'0123456789')


if __name__=='__main__':unittest.main()

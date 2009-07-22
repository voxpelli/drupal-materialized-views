<?php

define('MV_SORT_ASCENDING', 'ASC');
define('MV_SORT_DESCENDING', 'DEC');

/**
 * Master class used for describing materialized views. Usually returned from hook_materialized_view_info().
 */
class MaterializedView {
  protected $dynamic_filters = array();
  protected $static_filters = array();
  protected $extra_columns = array();
  protected $sort_sets = array();
  protected $name;

  /**
   * Constructs a MaterializedView object.
   *
   * @param $name
   *   The name of the materialized view.
   */
  public function __construct($name) {
    $this->name = $name;
  }

  /**
   * Gets the name of the materialized view.
   *
   * @returns
   *   The name of the materialized view.
   */
  public function getName() {
    return $this->name;
  }
  
  /**
   * Adds extra column that provides data consolidation but
   * is not indexed.
   *
   * @param $column
   *   MVColumn object to be used as the data source.
   */
  public function addExtraColumn(MVColumn $column) {
    $this->extra_columns[] = $column;
  }

  /**
   * Adds filter with a predefined test that runs when objects are updated.
   *
   * @param $column
   *   MVColumn object to be used for sorting.
   * @param $operation
   *   The operation to be used on this column.
   * @param $value
   *   The value or values to be used for the operation.
   */
  public function addStaticFilter(MVColumn $column, MVOperator $operator) {
    $filter = new stdClass();
    $filter->column = $column;
    $filter->operator = $operator;
    $this->static_filters[] = $filter;
  }
  
  /**
   * Adds filter configured to run at query time.
   *
   * @param $column
   *   MVColumn object to be used for sorting.
   * @param $operation
   *   The operation to be used on this column.
   */
  public function addDynamicFilter(MVColumn $column, MVOperator $operator) {
    $filter = new stdClass();
    $filter->column = $column;
    $filter->operator = $operator;
    $this->dynamic_filters[] = $filter;    
  }

  /**
   * Adds a sort set.
   *
   * @param $sort_set
   *   MVSortSet object to be used as a sort set.
   */
  public function addSortSet(MVSortSet $sort_set) {
    $this->sort_sets[] = $sort_set;    
  }

  /**
   * Gets the an array of the dynamic filters in the set.
   *
   * @returns
   *   An array of the filters.
   */
  public function getDynamicFilters() {
    return $this->dynamic_filters;
  }

  /**
   * Get an array listing columns to be indexed.
   */
  protected function getIndexes() {
    $indexes = array();
    $base_index = array();

    // TODO OPTIMIZATION: Sort columns in this order:
    // (1) Indexable conditions that don't end the index.
    // (2) Indexable conditions that end the index.
    // (3) Unindexable conditions.
    
    foreach ($this->dynamic_filters as $dynamic_filter) {
      $base_index[] = $dynamic_filter->column->getName();
      
      // TODO OPTIMIZATION: Stop building this index if we encounter a filter that ends the index.
    }
    
    if (empty($this->sort_sets)) {
      if (!empty($base_index)) {
        $indexes['mv_base'] = $base_index;
      }
    }
    else {    
      foreach ($this->sort_sets as $sort_set) {
        $index = $base_index;
        $sorts = $sort_set->getSorts();
  
        // TODO OPTIMIZATION: Apply effective order reversals to maximize ASC/DESC consistency.
  
        foreach ($sort_set->getSorts() as $sort) {
          $index[] = $sort->column->getName();      
          
          // TODO OPTIMIZATION: Stop building this index if the ASC/DESC direction changes.
        }
        $indexes[$sort_set->getName()] = $index;
      }
    }
    
    // TODO OPTIMIZATION: Prune indexes that are proper prefixes of other indexes.
    
    return $indexes;
  }
  
  /**
   * Build the schema for the materialized view table.
   */
  public function getSchema() {
    $schema = array();
    
    $schema['fields'] = array(
      'entity_type' => array(
        'type' => 'varchar',
        'length' => 64,
        'not null' => TRUE,
        'description' => 'The entity type.',
      ),
      'entity_id' => array(
        'type' => 'int',
        'not null' => TRUE,
        'default' => 0,
        'unsigned' => TRUE,
        'description' => 'The entity ID.',
      ),
    );

    // Add dynamic filter columns
    foreach ($this->dynamic_filters as $dynamic_filter) {
      $schema['fields'][$dynamic_filter->column->getName()] = $dynamic_filter->column->getSchema();
    }

    // Add sort columns
    foreach ($this->sort_sets as $sort_set) {
      foreach ($sort_set->getSorts() as $sort) {
        $schema['fields'][$sort->column->getName()] = $sort->column->getSchema();
      }
    }

    // Add extra columns
    foreach ($this->extra_columns as $extra_column) {
      $schema['fields'][$extra_column->getName()] = $extra_column->getSchema();
    }
    
    $schema['description'] = 'Materialized view storage for ' . $this->name . '.';
    $schema['indexes'] = $this->getIndexes();
    $schema['primary key'] = $this->getPrimaryKey();
    
    return $schema;
  }
  
  /**
   * Install the schema for this materialized view.
   */
  public function installSchema() {
    // TODO: Remove the ability to trample existing, non-MV tables.
  
    // First, remove all data for this MV.
    $this->uninstallSchema();
    
    // Generate and install the schema.
    $ret = array();
    $schema = $this->getSchema();
    db_create_table($ret, $this->name, $schema);

    // Rebuild the schema cache.
    drupal_get_schema(NULL, TRUE);

    // Add the schema hash to the database.
    $schema_hash = sha1(serialize($schema));
    db_query('INSERT INTO {materialized_view} (schema_hash, mvid) VALUES ("%s", "%s")', $schema_hash, $this->getName());

    watchdog('mv', 'Installed: ' . check_plain($this->getName()));
  }
  
  /**
   * Uninstall the schema for this materialized view.
   */
  public function uninstallSchema() {
    $ret = array();
    if (db_table_exists($this->name)) {
      db_drop_table($ret, $this->name);
    }

    // Clear any record of the installed schema.
    db_query('DELETE FROM {materialized_view} WHERE mvid = "%s"', $this->getName());

    // Reset all indexing by clearing all rows in the index tracker for this MV.
    db_query('DELETE FROM {materialized_view_indexing} WHERE mvid = "%s"', $this->getName());

    watchdog('mv', 'Uninstalled: ' . check_plain($this->getName()));
  }
  
  /**
   * Verify that the installed schema is current. If not, update it.
   */
  public function verifySchema() {
    $effect = FALSE;

    // Load the mview record from the database.
    $installed_schema_hash = db_result(db_query('SELECT schema_hash FROM {materialized_view} WHERE mvid = "%s"', $this->getName()));
    
    // Check that any schema for this MV are installed.
    if ($installed_schema_hash) {
      $schema_hash = sha1(serialize($this->getSchema()));
      // Check that schema are present and up-to-date.
      if ($schema_hash != $installed_schema_hash) {
        // On mismatch, (re)install the schema.
        $this->installSchema();
        $effect = TRUE;
      }
    }
    else {
      // Install the schema for the first time.
      $this->installSchema();
      $effect = TRUE;
    }
    
    return $effect;
  }
  
  /**
   * Index up to $batch_size items from each entity type.
   */
  public function index($batch_size) {
    $last_id = NULL;
    $existing_cursor = NULL;
  
    // Get a list of all entity types.
    $entity_types = materialized_view_entity_type_get();
    
    // Get indexing status for all entity types.
    $index_status = array();
    $res = db_query('SELECT entity_type, max_indexed_id FROM {materialized_view_indexing} WHERE mvid = "%s"', $this->getName());
    while ($row = db_fetch_array($res)) {
      if (array_key_exists($row['entity_type'], $entity_types)) {
        // Store the current indexing pointer with the entity type.
        $entity_types[$row['entity_type']]['max_indexed_id'] = $row['max_indexed_id'];
      }
      else {
        // Delete the indexing cursor if the entity type no longer exists.
        db_query('DELETE FROM {materialized_view_indexing} WHERE mvid = "%s" AND entity_type = "%s"', $this->getName(), $row['entity_type']);
      }
    }
    
    // Index a batch from each entity type.
    foreach ($entity_types as $entity_type_name => $entity_type_info) {
      // If there's no record of the maximum ID indexed, use one more than the current maximum.
      $existing_cursor = TRUE;
      if (!array_key_exists('max_indexed_id', $entity_type_info)) {
        $max_id_sql = 'SELECT MAX(' . db_escape_table($entity_type_info['id_column']) . ') FROM {' . db_escape_table($entity_type_info['id_table']) . '}';
        $entity_type_info['max_indexed_id'] = db_result(db_query($max_id_sql)) + 1;
        $existing_cursor = FALSE;
      }
      
      if ($entity_type_info['max_indexed_id'] > 1) {
        // Fetch a batch of IDs to index.
        $id_sql = 'SELECT t.' . db_escape_table($entity_type_info['id_column']) . ' AS id FROM {' . db_escape_table($entity_type_info['id_table']) . '} t WHERE t.' . db_escape_table($entity_type_info['id_column']) . ' < %d ORDER BY t.' . db_escape_table($entity_type_info['id_column']) . ' DESC';
        $res = db_query_range($id_sql, $entity_type_info['max_indexed_id'], 0, $batch_size);
        
        // TODO: Make i18n.
        $log_entry = 'Indexing "' . check_plain($entity_type_name) . '" with IDs under ' . check_plain($entity_type_info['max_indexed_id']) . '.';
        watchdog('mv', $log_entry);
        
        // Index the IDs.
        while ($row = db_fetch_array($res)) {
          $this->update($entity_type_name, $row['id']);
          $last_id = $row['id'];
        }
        
        // Update the indexing cursor.
        if ($existing_cursor) {
          db_query('UPDATE {materialized_view_indexing} SET max_indexed_id = %d WHERE mvid = "%s" AND entity_type = "%s"', $last_id, $this->getName(), $entity_type_name);
        }
        else {
          db_query('INSERT INTO {materialized_view_indexing} (max_indexed_id, mvid, entity_type) VALUES (%d, "%s", "%s")', $last_id, $this->getName(), $entity_type_name);
        }
      }
    } 
  }

  /**
   * Map changes using the active data sources and call updateMapped() to apply
   * each mapped change.
   *
   * @param $entity_type.
   *   The entity type.
   * @param $entity_id.
   *   The entity ID.
   */
  public function update($entity_type, $entity_id, $deletion_mode = FALSE) {
    $mappings = array();
    
    // Allow each source to map the change to affected entity type/ID pairs.
    foreach ($this->dynamic_filters as $source) {
      $mappings = array_merge_recursive($mappings, $source->column->getChangeMapping($entity_type, $entity_id));
    }
    foreach ($this->static_filters as $source) {
      $mappings = array_merge_recursive($mappings, $source->column->getChangeMapping($entity_type, $entity_id));
    }
    foreach ($this->extra_columns as $source) {
      $mappings = array_merge_recursive($mappings, $source->getChangeMapping($entity_type, $entity_id));
    }
    
    // TODO OPTIMIZATION: Only update the columns related to the change mappings.
    
    // In deletion mode, remove the actual deleted entity type/ID
    // (if present) from the update set.
    if ($deletion_mode && isset($mappings[$entity_type])) {
      $key = array_search($entity_id, $mappings[$entity_type]);
      if ($key !== FALSE) {
        unset($mappings[$entity_type][$key]);
      }
    }
    
    // For each unique entity type and ID, run the update.
    foreach ($mappings as $entity_type => $entity_ids) {
      foreach (array_unique($entity_ids, SORT_NUMERIC) as $entity_id) {
        $this->updateMapped($entity_type, $entity_id);
      }
    }
  }

  /**
   * Update an object in this materialized view or remove it
   * if it no longer meets criteria for inclusion.
   *
   * @param $entity_type.
   *   The entity type.
   * @param $entity_id.
   *   The entity ID.
   *
   * @returns A boolean value indicating whether the entity passed static filtering.
   */
  protected function updateMapped($entity_type, $entity_id) {
    // TODO: Start a transaction.
    
    // TODO OPTIMIZATION: Intelligently update rows instead of doing a DELETE+INSERT.
    
    // Delete the existing rows related to the object in the database.
    $this->delete($entity_type, $entity_id, FALSE);
  
    // Loop through filters to determine if the updated object qualifies for inclusion.
    foreach ($this->static_filters as $static_filter) {
    
      // Get the value of the column.
      $value = $static_filter->column->getValue($entity_type, $entity_id);
      
      // Apply the operator
      if (!$static_filter->operator->apply($value)) {

        // Return if the entity does not meet static criteria.
        return FALSE;
      }
    }
    
    // The entity meets static critieria; add its row(s) to the MV table.

    // We always need to write the entity_type and entity_id to the row.
    $keys = $write = array(
      'entity_type' => $entity_type,
      'entity_id' => $entity_id,
    );

    // Get values for the extra columns
    foreach ($this->extra_columns as $extra_columns) {
      $write[$extra_columns->getName()] = $extra_columns->getValue($entity_type, $entity_id);
    }
    
    // Get values for the sort columns
    foreach ($this->sort_sets as $sort_set) {
      foreach ($sort_set->getSorts() as $sort) {
        // Sort colummns are often used multiple places, but we should only generate them up once.
        if (!array_key_exists($sort->column->getName(), $write)) {
        
          // TODO: Throw exception if a sort column value is an array.
          $write[$sort->column->getName()] = $sort->column->getValue($entity_type, $entity_id);
        }
      }
    }

    // Get values for the dynamic filter columns
    foreach ($this->dynamic_filters as $dynamic_filter) {
      $write[$dynamic_filter->column->getName()] = $dynamic_filter->column->getValue($entity_type, $entity_id);
    }

    // Construct an iterator to perform the cartesian join.
    $iterator = new MVInstanceIterator($write);

    // Store the instances using a cartesian join.
    while ($instance = $iterator->getNextInstance()) {
      // Write the record to the database
      $schema = drupal_get_schema($this->name);
      
      $success = drupal_write_record($this->name, $instance);
      if (!$success) {
        // TODO: Make translatable.
        watchdog('mv', 'Updating the materialized view "' . check_plain($this->getName()) . '" failed:<pre>' . check_plain(print_r($instance, TRUE)) . '</pre>');
      }
    }
    
    return TRUE;
  }

  /**
   * Get an array listing the primary key columns.
   */
  protected function getPrimaryKey() {
    $columns = array('entity_type', 'entity_id');
    
    // Add dynamic filter columns as components of the primary key
    foreach ($this->dynamic_filters as $dynamic_filter) {
      $columns[] = $dynamic_filter->column->getName();
    }
    
    return $columns;
  }
  
  /**
   * Delete an object from this materialized view.
   *
   * @param $entity
   *   The entity.
   */
  public function delete($entity_type, $entity_id, $run_updates = TRUE) {
    // Delete rows related to the object.

    $sql = 'DELETE FROM {' . db_escape_table($this->name) . '}
            WHERE entity_type = "%s" AND entity_id = %d';
    db_query($sql, $entity_type, $entity_id);
    $num_deleted = db_affected_rows();

    // Apply updates to other items affected by the deletion.
    if ($run_updates) {
      $this->update($entity_type, $entity_id, TRUE);
    }
    
    return $num_deleted;
  }
}

/**
 * This class manages the hook_exit() indexing queue.
 */
final class MVJobQueue {
  static $update_jobs = array();
  static $deletion_jobs = array();
  
  /**
   * Update all materialized views for the given object.
   *
   * @param $entity
   *   The entity type ID.
   * @param $entity_id
   *   The entity ID.
   */
  public static function update($entity_type, $entity_id) {
    $job = new stdClass();
    $job->entity_type = $entity_type;
    $job->entity_id = $entity_id;
    self::$update_jobs[] = $job;
  }

  /**
   * Delete the given object from all materialized views.
   *
   * @param $entity
   *   The entity type ID.
   * @param $entity_id
   *   The entity ID.
   */
  public static function delete($entity_type, $entity_id) {
    $job = new stdClass();
    $job->entity_type = $entity_type;
    $job->entity_id = $entity_id;
    self::$deletion_jobs[] = $job;
  }

  public static function run() {
    // Don't do anything if no jobs are queued.
    if (empty(self::$update_jobs) && empty(self::$deletion_jobs)) {
      return;
    }
    
    // Ensure the MV schema are up to date.
    materialized_view_reconcile();
  
    // Load the materialized views.
    $materialized_views = materialized_view_get();
  
    // Run update jobs.
    foreach (self::$update_jobs as $job) {
      foreach ($materialized_views as $materialized_view) {
        $materialized_view->update($job->entity_type, $job->entity_id);
      }
    }

    // Run deletion jobs.
    foreach (self::$deletion_jobs as $job) {
      foreach ($materialized_views as $materialized_view) {
        $materialized_view->delete($job->entity_type, $job->entity_id);
      }
    }
  }
}

/**
 * This class handles iterations through multi-valued dynamic filter values. Internal use only.
 */
final class MVInstanceIterator {
  protected $instance_tracker;
  protected $all_values;
  protected $done;
  
  public function __construct($all_values) {
    $this->done = FALSE;
    $this->instance_tracker = array();
    $this->all_values = array();
    foreach ($all_values as $key => $value) {
      if (is_array($value)) {
        $this->all_values[$key] = $value;
      }
      else {
        $this->all_values[$key] = array($value);
      }
      $this->instance_tracker[$key] = 0;
    }
  }
  
  public function getNextInstance() {
    $instance = $this->getCurrentInstance();
    $this->incrementInstance();
    return $instance;
  }
  
  protected function incrementInstance() {
    foreach ($this->instance_tracker as $key => $value) {
      if ($value < count($this->all_values[$key])) {
        ++$this->instance_tracker[$key];
        if ($this->instance_tracker[$key] == count($this->all_values[$key])) {
          $this->instance_tracker[$key] = 0;
        }
        else {
          return;
        }
      }
    }
    
    $this->done = TRUE;
  }
  
  protected function getCurrentInstance() {
    if ($this->done) {
      return FALSE;
    }
  
    $return = array();
    foreach ($this->instance_tracker as $key => $value) {
      $return[$key] = $this->all_values[$key][$value];
    }
    return $return;
  }
}

/**
 * Defines a sort set that corresponds to ORDER BY criteria to be used on the table.
 *
 * Add to MVs using $mv->addSortSet($my_sort_set);
 */
class MVSortSet {
  protected $sorts = array();
  protected $name;
  protected $id;
  protected static $max_id = 0;
  
  public function __construct($name = NULL) {
    $this->id = self::$max_id++;
    $this->name = $name;
  }
  
  public function getName() {
    if (isset($this->name)) {
      return $this->name;
    }
    else {
      return 'mv_sort_set_' . $this->id;
    }
  }

  /**
   * Adds sort column configured to run at query time.
   *
   * @param $column
   *   MVColumn object to be used for sorting.
   * @param $direction
   *   Constant defining sort order. Use MV_SORT_ASCENDING
   *   or MV_SORT_DESCENDING.
   */
  public function addSort(MVColumn $column, $direction) {
    $sort = new stdClass();
    $sort->column = $column;
    $sort->direction = $direction;
    $this->sorts[] = $sort;
  }
  
  /**
   * Gets the an array of the sorts in the set.
   *
   * @returns
   *   An array of the sorts.
   */
  public function getSorts() {
    return $this->sorts;
  }
}

/**
 * Defines an operator for use in static and dynamic filters.
 *
 * Subclass to define usable operators.
 */
abstract class MVOperator {
  /**
   * Applies the operator to $value.
   *
   * @param $value
   *   The value to be compared.
   *
   * @returns boolean
   */
  abstract public function apply($value);

  /**
   * Determines whether the operator would end the
   * usefulness of an index if added to the index.
   *
   * @returns boolean
   */
  abstract public function endsIndex();

  /**
   * Determines whether the operator is indexable.
   *
   * @returns boolean
   */
  abstract public function indexable();
}

/**
 * Defines the equality operator for use in static and dynamic filters.
 */
class MVEqualityOperator extends MVOperator {
  protected $comparison_value = NULL;

  public function __construct($value = NULL) {
    $this->comparison_value = $value;
  }

  public function apply($value) {
    return $value == $this->comparison_value;
  }
  
  public function endsIndex() {
    return FALSE;
  }

  public function indexable() {
    return TRUE;
  }
}

/**
 * Defines a column data source for use in materialized views.
 *
 * Subclass to define usable data sources.
 */
abstract class MVColumn {
  /**
   * Retreive the value to use for this column, given an entity typ[e
   * and entity ID.
   *
   * @param $entity_type
   *   The entity type.
   * @param $entity_id
   *   The entity ID.
   */
  abstract public function getValue($entity_type, $entity_id);

  /**
   * For the given entity type and ID arguments, return an array
   * identifying types and IDs affected by the change. The default
   * implementation below assumes the this column object does not
   * aggregate data from other object types.
   *
   * A key example of why this function would be implemented differently
   * is the node to comment relationship for a column calculating
   * the timestamp of the most recent comment on a node. While
   * getValue() would only be called for nodes, creating of editing a comment
   * would potentially affect the value for each associated node. Thus,
   * an "timestamp of the most recent comment on a node" subclass
   * of MVColumn would return the associated node IDs for a given
   * comment and an empty array for any other entity type.
   *
   * @param $entity_type
   *   The entity type.
   * @param $entity_id
   *   The entity ID.
   *
   * @returns Mapping of types and IDs affected.
   */
  public function getChangeMapping($entity_type, $entity_id) {
    $changed = array();
    $changed[$entity_type] = array($entity_id);
    return $changed;
  }
  
  /**
   * Retrieve a column scheme suitable for use in $table['fields'].
   *
   * @returns The column scheme.
   */
  abstract public function getSchema();
  
  /**
   * Retrieve the column name.
   *
   * @returns A column name.
   */
  abstract public function getName();
}

class MVEntityCache {
  protected static $entities = array();
  
  public static function set($entity_type, $entity_id, $entity) {
    if (!isset(self::$entities[$entity_type])) {
      self::$entities[$entity_type] = array();
    }
    
    self::$entities[$entity_type][$entity_id] = $entity;
  }
  
  public static function get($entity_type, $entity_id, $autoload = TRUE) {
    if (isset(self::$entities[$entity_type][$entity_id])) {
      return self::$entities[$entity_type][$entity_id];
    }
    
    if ($autoload === FALSE) {
      return FALSE;
    }
    
    $load_function = $autoload;
    if ($autoload === TRUE) {
      $load_function = $entity_type . '_load';
    }
    self::$entities[$entity_type][$entity_id] = $load_function($entity_id);
    return $entities[$entity_type][$entity_id];
  }
}